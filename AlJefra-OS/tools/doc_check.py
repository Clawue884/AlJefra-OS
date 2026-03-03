#!/usr/bin/env python3
"""AlJefra OS — Documentation Consistency Checker

Extracts ground truth from source code and validates all documentation
(HTML, Markdown, README) against it.  Reports mismatches with file:line
references and can auto-correct fixable issues.

Usage:
    python3 tools/doc_check.py                # Check mode (default)
    python3 tools/doc_check.py --fix          # Auto-correct documentation
    python3 tools/doc_check.py --json         # Machine-readable output
    python3 tools/doc_check.py --verbose      # Show ground truth values

Exit codes:
    0  All documentation is consistent
    1  Mismatches found (or fixed with --fix)
    2  Script error (missing repo root, etc.)

No external dependencies — pure Python 3.6+.
"""

import argparse
import json
import os
import re
import sys
from pathlib import Path

# ── Tolerance for numeric claims ────────────────────────────────────
# "67,000+" is valid for actual 67,295.  Claims within TOLERANCE of the
# ground truth are accepted.  Claims that are exact or use "~" / "+"
# get additional slack.
TOLERANCE_PCT = 5  # percent


# ═══════════════════════════════════════════════════════════════════════
#  Mismatch Record
# ═══════════════════════════════════════════════════════════════════════

class Mismatch:
    __slots__ = (
        "file", "line", "category", "expected", "actual",
        "context", "fixable", "fix_old", "fix_new",
    )

    def __init__(self, *, file, line, category, expected, actual, context,
                 fixable=False, fix_old=None, fix_new=None):
        self.file = str(file)
        self.line = line
        self.category = category
        self.expected = expected
        self.actual = actual
        self.context = context
        self.fixable = fixable
        self.fix_old = fix_old
        self.fix_new = fix_new

    def __str__(self):
        tag = "[FIXABLE]" if self.fixable else "[MANUAL] "
        return f"{tag} {self.file}:{self.line}: {self.category}: expected {self.expected!r}, found {self.actual!r}"

    def to_dict(self):
        return {
            "file": self.file,
            "line": self.line,
            "category": self.category,
            "expected": self.expected,
            "actual": self.actual,
            "context": self.context,
            "fixable": self.fixable,
        }


# ═══════════════════════════════════════════════════════════════════════
#  Helper utilities
# ═══════════════════════════════════════════════════════════════════════

def find_repo_root():
    """Locate the AlJefra OS repository root."""
    # 1. Script is at tools/doc_check.py → parent is repo root
    script_dir = Path(__file__).resolve().parent
    candidate = script_dir.parent
    if (candidate / "store" / "package.h").exists():
        return candidate
    # 2. Current working directory
    cwd = Path.cwd()
    if (cwd / "store" / "package.h").exists():
        return cwd
    print("ERROR: Cannot find repo root (expected store/package.h).", file=sys.stderr)
    print("       Run from the repo root or use --repo-root.", file=sys.stderr)
    sys.exit(2)


def count_lines(path):
    """Count lines in a single file.  Returns 0 on error."""
    try:
        with open(path, "r", errors="replace") as fh:
            return sum(1 for _ in fh)
    except OSError:
        return 0


def sum_lines(paths):
    """Sum line counts across an iterable of Paths."""
    return sum(count_lines(p) for p in paths)


def rglob_files(root, suffixes, exclude_parts=None):
    """Recursively find files by suffix, optionally excluding path segments."""
    exclude_parts = exclude_parts or []
    for p in root.rglob("*"):
        if not p.is_file():
            continue
        if p.suffix not in suffixes:
            continue
        parts_str = str(p)
        if any(ex in parts_str for ex in exclude_parts):
            continue
        yield p


def within_tolerance(claimed, actual, pct=TOLERANCE_PCT):
    """Return True if *claimed* is within *pct*% of *actual*."""
    if actual == 0:
        return claimed == 0
    return abs(claimed - actual) / actual * 100 <= pct


# ═══════════════════════════════════════════════════════════════════════
#  Ground Truth Extraction
# ═══════════════════════════════════════════════════════════════════════

def extract_ground_truth(repo):
    """Build a dict of ground truth metrics from the source code."""
    truth = {}

    # ── .ajdrv package format ──────────────────────────────────────
    pkg_h = repo / "store" / "package.h"
    if pkg_h.exists():
        src = pkg_h.read_text()
        m = re.search(r"#define\s+AJDRV_MAGIC\s+(0x[0-9A-Fa-f]+)", src)
        if m:
            # Normalise to 0x prefix with uppercase hex digits
            truth["ajdrv_magic_hex"] = "0x" + m.group(1)[2:].upper()
            # Derive ASCII: 0x56444A41 → bytes → "AJDV"
            try:
                raw = int(m.group(1), 16).to_bytes(4, "little")
                truth["ajdrv_magic_ascii"] = raw.decode("ascii")
            except Exception:
                pass
        for tag in ("X86_64", "AARCH64", "RISCV64", "ANY"):
            m = re.search(rf"#define\s+AJDRV_ARCH_{tag}\s+(\S+)", src)
            if m:
                truth[f"arch_code_{tag.lower()}"] = m.group(1)

    # ── TLS version ───────────────────────────────────────────────
    bearssl = repo / "programs" / "netstack" / "bearssl" / "inc" / "bearssl_ssl.h"
    if bearssl.exists():
        src = bearssl.read_text()
        if re.search(r"#define\s+BR_TLS13\b", src):
            truth["max_tls"] = "1.3"
        elif re.search(r"#define\s+BR_TLS12\b", src):
            truth["max_tls"] = "1.2"

    # ── File & driver counts ──────────────────────────────────────
    driver_dir = repo / "drivers"
    if driver_dir.exists():
        driver_files = sorted(driver_dir.rglob("*.c"))
        truth["driver_count"] = len(driver_files)
        truth["driver_names"] = [f.stem for f in driver_files]

    hal_dir = repo / "hal"
    if hal_dir.exists():
        truth["hal_header_count"] = len(list(hal_dir.glob("*.h")))

    # Total files (excluding .git)
    all_files = [p for p in repo.rglob("*") if p.is_file() and ".git" not in p.parts]
    truth["total_files"] = len(all_files)

    # Source files (excluding BearSSL and Python tooling)
    src_exts = {".c", ".h", ".asm", ".S"}
    src_files = list(rglob_files(repo, src_exts, exclude_parts=["bearssl"]))
    truth["source_file_count"] = len(src_files)

    # ── Line counts ───────────────────────────────────────────────
    truth["original_loc"] = sum_lines(src_files)

    bearssl_dir = repo / "programs" / "netstack" / "bearssl"
    if bearssl_dir.exists():
        truth["bearssl_loc"] = sum_lines(rglob_files(bearssl_dir, {".c", ".h"}))

    # Per-component
    def dir_lines(rel, exts=(".c", ".h")):
        d = repo / rel
        return sum_lines(d.rglob("*.[ch]")) if d.exists() else 0

    def file_lines(rel):
        f = repo / rel
        return count_lines(f) if f.exists() else 0

    truth["gui_lines"] = dir_lines("gui")
    truth["ai_chat_lines"] = file_lines("kernel/ai_chat.c")
    truth["verify_c_lines"] = file_lines("store/verify.c")
    truth["store_lines"] = dir_lines("store")
    truth["net_lines"] = dir_lines("net")
    truth["dhcp_lines"] = file_lines("net/dhcp.c")

    # x86 ASM kernel (may be at src/ or aljefra/src/)
    src_asm = 0
    for base in ["src", "aljefra/src"]:
        base_dir = repo / base
        if base_dir.exists():
            src_asm += sum_lines(rglob_files(base_dir, {".asm"}))
    truth["x86_asm_lines"] = src_asm

    # HAL + arch ports
    hal_arch_lines = dir_lines("hal")
    arch_d = repo / "arch"
    if arch_d.exists():
        hal_arch_lines += sum_lines(rglob_files(arch_d, {".c", ".h", ".S"}))
    truth["hal_arch_lines"] = hal_arch_lines

    # Driver lines
    truth["driver_lines"] = dir_lines("drivers")

    # ── Architecture count ────────────────────────────────────────
    makefile = repo / "Makefile"
    if makefile.exists():
        src = makefile.read_text()
        # Use "else ifeq" + first "ifeq" to get unique arch names
        archs = []
        for m in re.finditer(r"(?:else\s+)?ifeq\s+\(\$\(ARCH\),(\w+)\)", src):
            name = m.group(1)
            if name not in archs:
                archs.append(name)
        truth["arch_count"] = len(archs)
        truth["arch_names"] = archs

    # ── Evolution stats ───────────────────────────────────────────
    evo_log = repo / "evolution" / "logs" / "evolution_log.jsonl"
    if evo_log.exists():
        raw_lines = evo_log.read_text().strip().split("\n")
        truth["evolution_entries"] = len(raw_lines)
        gens = set()
        for raw in raw_lines:
            try:
                obj = json.loads(raw)
                if "generation" in obj:
                    gens.add(obj["generation"])
            except (json.JSONDecodeError, KeyError):
                pass
        truth["evolution_generations"] = max(gens) if gens else 0

    # ── Marketplace endpoints ─────────────────────────────────────
    app_py = repo / "server" / "app.py"
    if app_py.exists():
        src = app_py.read_text()
        truth["marketplace_routes"] = len(re.findall(r"@app\.route", src))
        # Unique endpoint paths (excluding index /)
        route_paths = set()
        for rm in re.finditer(r"@app\.route\(['\"]([^'\"]+)['\"]", src):
            rp = rm.group(1)
            if rp != "/":
                route_paths.add(rp)
        truth["marketplace_unique_endpoints"] = len(route_paths)

    # ── Net stack total (net/ + programs/netstack/ excluding BearSSL) ──
    net_stack_total = dir_lines("net")
    netstack_dir = repo / "programs" / "netstack"
    if netstack_dir.exists():
        net_stack_total += sum_lines(
            rglob_files(netstack_dir, {".c", ".h"}, exclude_parts=["bearssl"])
        )
    truth["net_stack_lines"] = net_stack_total

    # ── AI bootstrap lines ────────────────────────────────────────
    truth["ai_bootstrap_lines"] = file_lines("kernel/ai_bootstrap.c")

    # ── Marketplace server lines (Python) ─────────────────────────
    server_dir = repo / "server"
    if server_dir.exists():
        truth["marketplace_server_lines"] = sum_lines(server_dir.glob("*.py"))

    # ── Evolution framework lines ─────────────────────────────────
    evo_dir = repo / "evolution"
    if evo_dir.exists():
        truth["evolution_lines"] = sum_lines(
            rglob_files(evo_dir, {".c", ".h", ".py", ".json", ".jsonl"})
        )

    # ── Website lines ─────────────────────────────────────────────
    web_dir = repo / "website"
    if web_dir.exists():
        truth["website_lines"] = sum_lines(
            rglob_files(web_dir, {".html", ".css", ".js"})
        )

    # ── Action type count (from ai_chat.h enum) ──────────────────
    ai_chat_h = repo / "kernel" / "ai_chat.h"
    if ai_chat_h.exists():
        h_src = ai_chat_h.read_text()
        enum_m = re.search(r"enum\s*\{([^}]+)\}", h_src, re.DOTALL)
        if enum_m:
            action_names = set(re.findall(
                r"\b(ACTION_(?!NONE\b|COUNT\b|NAME_COUNT\b)\w+)",
                enum_m.group(1),
            ))
            truth["action_type_count"] = len(action_names)

    return truth


# ═══════════════════════════════════════════════════════════════════════
#  Documentation Scanners
# ═══════════════════════════════════════════════════════════════════════

def _add(mismatches, **kw):
    mismatches.append(Mismatch(**kw))


def check_tls_version(filepath, lines, truth, mismatches):
    """Flag any 'TLS 1.3' when BearSSL only supports TLS 1.2."""
    if truth.get("max_tls") != "1.2":
        return
    for i, line in enumerate(lines, 1):
        if re.search(r"TLS\s*1\.3", line):
            _add(mismatches,
                 file=filepath, line=i,
                 category="TLS version",
                 expected="TLS 1.2",
                 actual="TLS 1.3",
                 context=line.strip(),
                 fixable=True,
                 fix_old="TLS 1.3",
                 fix_new="TLS 1.2")


def check_magic_number(filepath, lines, truth, mismatches):
    """Flag wrong .ajdrv magic values."""
    expected_hex = truth.get("ajdrv_magic_hex")
    expected_ascii = truth.get("ajdrv_magic_ascii")
    if not expected_hex:
        return

    known_wrong_hex = {"0x414A4452", "0x52444A41"}
    known_wrong_ascii = {'"AJDR"'}

    for i, line in enumerate(lines, 1):
        for wrong in known_wrong_hex:
            if wrong in line:
                _add(mismatches,
                     file=filepath, line=i,
                     category=".ajdrv magic",
                     expected=expected_hex,
                     actual=wrong,
                     context=line.strip(),
                     fixable=True,
                     fix_old=wrong,
                     fix_new=expected_hex)

        if expected_ascii == "AJDV":
            for wrong in known_wrong_ascii:
                if wrong in line and "AJDV" not in line and "AJDRV" not in line:
                    _add(mismatches,
                         file=filepath, line=i,
                         category=".ajdrv magic ASCII",
                         expected='"AJDV"',
                         actual=wrong,
                         context=line.strip(),
                         fixable=True,
                         fix_old='"AJDR"',
                         fix_new='"AJDV"')


def check_arch_encoding(filepath, lines, truth, mismatches):
    """Flag 1-indexed arch codes when package.h uses 0-indexed."""
    if truth.get("arch_code_x86_64") != "0":
        return
    for i, line in enumerate(lines, 1):
        # Patterns like "1 = x86_64" or "1: x86_64" in table context
        if re.search(r"\b1\s*[=|]\s*x86.64", line, re.IGNORECASE):
            if re.search(r"\b2\s*[=|]\s*aarch64", line, re.IGNORECASE) or \
               re.search(r"\b2\s*[=|]\s*aarch64", "\n".join(lines[i:i+3]), re.IGNORECASE):
                _add(mismatches,
                     file=filepath, line=i,
                     category="arch encoding",
                     expected="0-indexed (0=x86_64, 1=aarch64, 2=riscv64)",
                     actual="1-indexed encoding",
                     context=line.strip(),
                     fixable=False)


def check_numeric_claim(filepath, line_no, line_text, pattern, category,
                         truth_key, truth, mismatches, tolerance_pct=TOLERANCE_PCT):
    """Generic check: find a number in *line_text* matching *pattern* and compare
    it to ``truth[truth_key]``.  Numbers with ``~`` or ``+`` suffix get extra
    tolerance."""
    actual = truth.get(truth_key)
    if actual is None:
        return
    for m in re.finditer(pattern, line_text):
        claimed_str = m.group(1).replace(",", "")
        try:
            claimed = int(claimed_str)
        except ValueError:
            continue
        if not within_tolerance(claimed, actual, tolerance_pct):
            _add(mismatches,
                 file=filepath, line=line_no,
                 category=category,
                 expected=str(actual),
                 actual=str(claimed),
                 context=line_text.strip())


def check_driver_count(filepath, lines, truth, mismatches):
    """Flag driver count claims that don't match drivers/**/*.c count."""
    expected = truth.get("driver_count")
    if expected is None:
        return
    for i, line in enumerate(lines, 1):
        # Match "22 Portable C Drivers", "22+ Drivers", etc.
        # Skip small numbers like "top 5 drivers" (contextual, not a count claim)
        for m in re.finditer(r"(\d+)\+?\s*(?:[Pp]ortable\s+)?(?:C\s+)?[Dd]rivers", line):
            claimed = int(m.group(1))
            if claimed < 10:
                continue  # Skip small numbers (e.g. "top 5 drivers")
            if claimed != expected and abs(claimed - expected) > 2:
                _add(mismatches,
                     file=filepath, line=i,
                     category="driver count",
                     expected=str(expected),
                     actual=str(claimed),
                     context=line.strip())


def check_hal_header_count(filepath, lines, truth, mismatches):
    """Flag HAL header count claims."""
    expected = truth.get("hal_header_count")
    if expected is None:
        return
    for i, line in enumerate(lines, 1):
        if re.search(r"HAL\s+header", line, re.IGNORECASE):
            check_numeric_claim(filepath, i, line,
                                r"(\d+)\s*(?:header|\bHAL\b)",
                                "HAL header count", "hal_header_count",
                                truth, mismatches, tolerance_pct=0)


def check_arch_count(filepath, lines, truth, mismatches):
    """Flag architecture count claims."""
    expected = truth.get("arch_count")
    if expected is None:
        return
    for i, line in enumerate(lines, 1):
        # Match "3 Architectures" but NOT "64 architectures" from "RISC-V 64"
        for m in re.finditer(r"(?<!\w)(\d{1,2})\s+[Aa]rchitectures", line):
            claimed = int(m.group(1))
            if claimed > 20:
                continue  # Skip; likely part of an arch name like "64"
            if claimed != expected:
                _add(mismatches,
                     file=filepath, line=i,
                     category="architecture count",
                     expected=str(expected),
                     actual=str(claimed),
                     context=line.strip())


def check_total_loc(filepath, lines, truth, mismatches):
    """Flag total lines-of-code claims that are far from actual."""
    actual = truth.get("original_loc")
    if actual is None:
        return
    for i, line in enumerate(lines, 1):
        # Match patterns like "67,295 lines of original C and Assembly",
        # "67,000+ Lines of Code", "~61,000 lines (original code)"
        # Only for large numbers (> 10k) to avoid component-level claims.
        # Matches: "N lines of code", "N lines of original code",
        #          "N lines (original code)", "N lines of original C and Assembly"
        pat = r"([\d,]+)\+?\s*[Ll]ines\s+(?:of\s+)?(?:\(?[Oo]riginal\s+)?(?:[Cc]ode|C\s+and\s+[Aa]ssembly)"
        for m in re.finditer(pat, line):
            claimed = int(m.group(1).replace(",", ""))
            if claimed < 10000:
                continue  # Skip component-level line counts
            if not within_tolerance(claimed, actual, 5):
                old_num = m.group(1)
                # Preserve format: round to 1000 for approximate claims
                if claimed % 1000 == 0:
                    fix_actual = (actual // 1000) * 1000
                else:
                    fix_actual = actual
                new_num = f"{fix_actual:,}"
                _add(mismatches,
                     file=filepath, line=i,
                     category="total LOC",
                     expected=f"~{actual:,}",
                     actual=f"{claimed:,}",
                     context=line.strip(),
                     fixable=True,
                     fix_old=old_num,
                     fix_new=new_num)


def check_file_count(filepath, lines, truth, mismatches):
    """Flag total file count claims."""
    actual = truth.get("total_files")
    if actual is None:
        return
    for i, line in enumerate(lines, 1):
        for m in re.finditer(r"([\d,]+)\+?\s*[Ff]iles\b", line):
            claimed = int(m.group(1).replace(",", ""))
            # Skip small numbers like "6 files" (probably a different context)
            if claimed < 100:
                continue
            if not within_tolerance(claimed, actual, 10):
                _add(mismatches,
                     file=filepath, line=i,
                     category="total file count",
                     expected=f"~{actual:,}",
                     actual=f"{claimed:,}",
                     context=line.strip())


def check_source_file_count(filepath, lines, truth, mismatches):
    """Flag source file count claims."""
    actual = truth.get("source_file_count")
    if actual is None:
        return
    for i, line in enumerate(lines, 1):
        # Only match explicit "Source files" headers/table rows, not prose
        if re.search(r"[Ss]ource\s+files\s*\|", line) or \
           re.match(r"\|\s*[Ss]ource\s+files", line):
            for m in re.finditer(r"(\d[\d,]*)", line):
                claimed = int(m.group(1).replace(",", ""))
                if claimed < 50:
                    continue
                if not within_tolerance(claimed, actual, 10):
                    _add(mismatches,
                         file=filepath, line=i,
                         category="source file count",
                         expected=str(actual),
                         actual=str(claimed),
                         context=line.strip())


def check_bearssl_loc(filepath, lines, truth, mismatches):
    """Flag BearSSL line count claims."""
    actual = truth.get("bearssl_loc")
    if actual is None:
        return
    for i, line in enumerate(lines, 1):
        # Look for "N lines ... BearSSL" or "BearSSL ... N lines"
        # but only consider the number closest to "BearSSL"
        bm = re.search(r"[Bb]ear\s*SSL", line)
        if not bm:
            continue
        bearssl_pos = bm.start()
        best_match = None
        best_dist = float("inf")
        for m in re.finditer(r"([\d,]+)\s*lines", line):
            claimed = int(m.group(1).replace(",", ""))
            if claimed < 10000:
                continue
            dist = abs(m.start() - bearssl_pos)
            if dist < best_dist:
                best_dist = dist
                best_match = claimed
        if best_match is not None and not within_tolerance(best_match, actual, 5):
            _add(mismatches,
                 file=filepath, line=i,
                 category="BearSSL LOC",
                 expected=f"{actual:,}",
                 actual=f"{best_match:,}",
                 context=line.strip())


def check_component_lines(filepath, lines, truth, mismatches):
    """Flag per-component line count claims that are significantly wrong.

    Each check is a tuple of:
      (keyword_pattern, truth_key, category, exclude_pattern)
    The exclude_pattern prevents false positives where the keyword appears
    in a sentence about a different component.
    """
    checks = [
        (r"\bai_chat\.c\b", "ai_chat_lines", "AI chat lines", None),
        (r"\bverify\.c\b", "verify_c_lines", "verify.c lines", None),
        (r"\bdhcp\.c\b", "dhcp_lines", "DHCP lines", None),
        # GUI: match "GUI system totals N lines" but not "AI chat" context
        (r"\bGUI\s+system\b|\bgui/\b", "gui_lines", "GUI lines", None),
    ]
    for i, line in enumerate(lines, 1):
        for keyword_pat, truth_key, cat, excl in checks:
            actual = truth.get(truth_key)
            if actual is None or actual == 0:
                continue
            if not re.search(keyword_pat, line, re.IGNORECASE):
                continue
            if excl and re.search(excl, line, re.IGNORECASE):
                continue
            for m in re.finditer(r"([\d,]+)\s*lines", line):
                claimed = int(m.group(1).replace(",", ""))
                if claimed < 50:
                    continue
                if not within_tolerance(claimed, actual, 15):
                    _add(mismatches,
                         file=filepath, line=i,
                         category=cat,
                         expected=f"~{actual:,}",
                         actual=f"{claimed:,}",
                         context=line.strip())


def check_html_stat_boxes(filepath, lines, truth, mismatches):
    """Check standalone numbers in HTML stat/metric boxes.

    Detects consecutive-line patterns like:
        <div ...>61,657</div>
        <div ...>Lines of Original Code</div>
    """
    LABEL_MAP = [
        # (label_regex, truth_key, exact_match)
        (r"Lines\s+of\s+Original\s+Code", "original_loc", True),
        (r"Lines\s+of\s+Code",            "original_loc", False),
        (r"v1\.0\s+Target",               "original_loc", False),
    ]
    for i, line in enumerate(lines, 1):
        m = re.search(r">(~?[\d,]+\+?)</", line)
        if not m:
            continue
        raw = m.group(1)
        num_str = raw.replace(",", "").replace("+", "").replace("~", "")
        try:
            claimed = int(num_str)
        except ValueError:
            continue
        if claimed < 1000:
            continue
        # Check next line for a label
        if i >= len(lines):
            continue
        next_line = lines[i]  # 0-indexed: lines[i] is line i+1
        for label_pat, truth_key, exact in LABEL_MAP:
            if not re.search(label_pat, next_line, re.IGNORECASE):
                continue
            actual = truth.get(truth_key)
            if actual is None:
                continue
            if exact:
                if claimed != actual:
                    new_val = f"{actual:,}"
                    _add(mismatches,
                         file=filepath, line=i,
                         category="stat box (exact LOC)",
                         expected=new_val,
                         actual=raw,
                         context=line.strip(),
                         fixable=True,
                         fix_old=f">{raw}</",
                         fix_new=f">{new_val}</")
            else:
                approx = (actual // 1000) * 1000
                has_plus = "+" in raw
                if claimed != approx:
                    new_val = f"{approx:,}" + ("+" if has_plus else "")
                    _add(mismatches,
                         file=filepath, line=i,
                         category="stat box (approx LOC)",
                         expected=new_val,
                         actual=raw,
                         context=line.strip(),
                         fixable=True,
                         fix_old=f">{raw}</",
                         fix_new=f">{new_val}</")
            break


def check_roadmap_component_table(filepath, lines, truth, mismatches):
    """Check per-component line counts in HTML table rows.

    Matches roadmap rows like:
        <tr><td>x86-64 ASM Kernel</td><td>9,102</td><td ...>Status</td></tr>
    and validates the number against the corresponding ground truth.
    """
    COMPONENT_MAP = [
        (r"x86.64\s+ASM\s+Kernel",                    "x86_asm_lines"),
        (r"HAL\s*\+\s*\d+\s+Architecture\s+Ports?",   "hal_arch_lines"),
        (r"\d+\s+Portable\s+C\s+Drivers",             "driver_lines"),
        (r"TCP/IP",                                    "net_stack_lines"),
        (r"Ed25519\s+Cryptography",                    "verify_c_lines"),
        (r"AI\s+Agent.*Claude",                        "ai_bootstrap_lines"),
        (r"Driver\s+Marketplace\s+Server",             "marketplace_server_lines"),
        (r"AI\s+Evolution\s+Framework",                "evolution_lines"),
        (r"Website.*aljefra",                          "website_lines"),
    ]
    for i, line in enumerate(lines, 1):
        for comp_pat, truth_key in COMPONENT_MAP:
            actual = truth.get(truth_key)
            if actual is None or actual == 0:
                continue
            if not re.search(comp_pat, line, re.IGNORECASE):
                continue
            # Find the first numeric <td> after the component name (handles ~N prefix)
            td_nums = re.findall(r"</td>\s*<td[^>]*>\s*~?([\d,]+)\s*</td>", line)
            if not td_nums:
                continue
            claimed_str = td_nums[0].replace(",", "")
            try:
                claimed = int(claimed_str)
            except ValueError:
                continue
            if claimed < 50:
                continue
            if not within_tolerance(claimed, actual, 0.5):
                old_val = td_nums[0]
                new_val = f"{actual:,}"
                # Handle ~N prefix in original HTML
                td_match = re.search(
                    r"</td>\s*<td[^>]*>\s*(~?" + re.escape(old_val) + r")\s*</td>",
                    line,
                )
                fix_old_val = td_match.group(1) if td_match else old_val
                _add(mismatches,
                     file=filepath, line=i,
                     category=f"roadmap component ({truth_key})",
                     expected=new_val,
                     actual=fix_old_val,
                     context=line.strip()[:120],
                     fixable=True,
                     fix_old=f">{fix_old_val}</td>",
                     fix_new=f">{new_val}</td>")
            break


def check_inline_numeric_claims(filepath, lines, truth, mismatches):
    """Check inline numeric claims in prose/HTML text.

    Catches patterns like '1,547 lines of pure C cryptography',
    '19 system actions', '9-header HAL', 'N Endpoints', 'N Generations'.
    """
    CHECKS = [
        (r"([\d,]+)\s+lines?\s+of\s+(?:pure\s+)?C\s+cryptography",
         "verify_c_lines", "Ed25519 inline LOC"),
        (r"(\d+)\s+system\s+actions?",
         "action_type_count", "action type count"),
        (r"(\d+)-header\s+HAL",
         "hal_header_count", "HAL header inline count"),
        (r"(\d+)\s+[Ee]ndpoints?",
         "marketplace_unique_endpoints", "endpoint count"),
        (r"(\d+)\s+[Gg]enerations?",
         "evolution_generations", "evolution generations"),
    ]
    for i, line in enumerate(lines, 1):
        for pat, truth_key, cat in CHECKS:
            actual = truth.get(truth_key)
            if actual is None:
                continue
            for m in re.finditer(pat, line):
                claimed_str = m.group(1).replace(",", "")
                try:
                    claimed = int(claimed_str)
                except ValueError:
                    continue
                if claimed != actual:
                    old_full = m.group(0)
                    old_num = m.group(1)
                    new_num = f"{actual:,}" if actual >= 1000 else str(actual)
                    new_full = old_full.replace(old_num, new_num, 1)
                    _add(mismatches,
                         file=filepath, line=i,
                         category=cat,
                         expected=str(actual),
                         actual=str(claimed),
                         context=line.strip()[:120],
                         fixable=True,
                         fix_old=old_full,
                         fix_new=new_full)


def check_badge_claims(filepath, lines, truth, mismatches):
    """Check shield.io badge URLs for line count claims.

    Matches URL-encoded patterns like:
        lines%20of%20code-67%2C295-orange.svg
    """
    actual = truth.get("original_loc")
    if actual is None:
        return
    for i, line in enumerate(lines, 1):
        for m in re.finditer(r"lines%20of%20code-([\d%A-Fa-f]+)-", line):
            claimed_str = m.group(1).replace("%2C", ",").replace(",", "")
            try:
                claimed = int(claimed_str)
            except ValueError:
                continue
            if not within_tolerance(claimed, actual, 5):
                old_val = m.group(1)
                new_val = f"{actual:,}".replace(",", "%2C")
                _add(mismatches,
                     file=filepath, line=i,
                     category="badge LOC",
                     expected=f"{actual:,}",
                     actual=f"{claimed:,}",
                     context=line.strip()[:120],
                     fixable=True,
                     fix_old=f"lines%20of%20code-{old_val}-",
                     fix_new=f"lines%20of%20code-{new_val}-")


# ═══════════════════════════════════════════════════════════════════════
#  Main scan orchestrator
# ═══════════════════════════════════════════════════════════════════════

ALL_CHECKS = [
    check_tls_version,
    check_magic_number,
    check_arch_encoding,
    check_badge_claims,
    check_driver_count,
    check_hal_header_count,
    check_arch_count,
    check_total_loc,
    check_file_count,
    check_source_file_count,
    check_bearssl_loc,
    check_component_lines,
    check_html_stat_boxes,
    check_roadmap_component_table,
    check_inline_numeric_claims,
]


def scan_file(filepath, truth, mismatches):
    """Run all checks on a single documentation file."""
    try:
        text = Path(filepath).read_text(errors="replace")
    except OSError:
        return
    lines = text.split("\n")
    for check in ALL_CHECKS:
        check(filepath, lines, truth, mismatches)


def scan_docs(truth, doc_paths):
    """Scan a list of files and directories for mismatches."""
    # Directories to skip (experiments, build artifacts, third-party code)
    SKIP_DIRS = {
        ".git", "node_modules", "build", "bearssl",
        "experiment_a", "experiment_b",
    }

    mismatches = []
    seen = set()

    for path in doc_paths:
        p = Path(path)
        if not p.exists():
            continue

        if p.is_file():
            rp = p.resolve()
            if rp not in seen:
                seen.add(rp)
                scan_file(rp, truth, mismatches)
        else:
            for ext in ("*.html", "*.md"):
                for f in sorted(p.rglob(ext)):
                    if any(skip in f.parts for skip in SKIP_DIRS):
                        continue
                    rp = f.resolve()
                    if rp not in seen:
                        seen.add(rp)
                        scan_file(rp, truth, mismatches)

    return mismatches


# ═══════════════════════════════════════════════════════════════════════
#  Fix mode
# ═══════════════════════════════════════════════════════════════════════

def fix_docs(mismatches):
    """Apply auto-corrections for fixable mismatches.  Returns count of fixes."""
    # Group by file to minimise I/O
    by_file = {}
    for m in mismatches:
        if m.fixable and m.fix_old and m.fix_new:
            by_file.setdefault(m.file, []).append(m)

    fixed = 0
    for filepath, fixes in by_file.items():
        try:
            content = Path(filepath).read_text(errors="replace")
            original = content
            for fix in fixes:
                if fix.fix_old in content:
                    content = content.replace(fix.fix_old, fix.fix_new)
                    fixed += 1
            if content != original:
                Path(filepath).write_text(content)
        except OSError as exc:
            print(f"  ERROR writing {filepath}: {exc}", file=sys.stderr)
    return fixed


# ═══════════════════════════════════════════════════════════════════════
#  CLI
# ═══════════════════════════════════════════════════════════════════════

def auto_detect_website_dir(repo):
    """Try common locations for the AlJefra OS website HTML files."""
    for candidate in [
        repo / "website",                  # website/ subdirectory of repo
        repo.parent.parent,               # os_ai/AlJefra-OS → os_ai → aljefra_os
        repo.parent,                       # one level up
    ]:
        c = candidate.resolve()
        if (c / "index.html").exists() and (c / "roadmap.html").exists():
            return c
    return None


def main():
    ap = argparse.ArgumentParser(
        description="AlJefra OS Documentation Consistency Checker",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    ap.add_argument("--fix", action="store_true",
                    help="Auto-correct fixable mismatches in-place")
    ap.add_argument("--json", action="store_true",
                    help="Machine-readable JSON output")
    ap.add_argument("--verbose", "-v", action="store_true",
                    help="Print extracted ground truth values")
    ap.add_argument("--repo-root", metavar="DIR",
                    help="Path to the code repository root")
    ap.add_argument("--website-dir", metavar="DIR",
                    help="Path to the website HTML directory")
    args = ap.parse_args()

    # ── Locate repo root ──────────────────────────────────────────
    if args.repo_root:
        repo = Path(args.repo_root).resolve()
    else:
        repo = find_repo_root()

    # ── Extract ground truth ──────────────────────────────────────
    truth = extract_ground_truth(repo)

    if args.verbose and not args.json:
        print("Ground truth extracted from source code:")
        print("-" * 50)
        for k, v in sorted(truth.items()):
            if isinstance(v, list) and len(v) > 10:
                print(f"  {k}: [{len(v)} items]")
            else:
                print(f"  {k}: {v}")
        print("-" * 50)
        print()

    # ── Collect documentation paths to scan ───────────────────────
    doc_paths = [
        repo / "doc",
        repo / "README.md",
    ]

    website_dir = None
    if args.website_dir:
        website_dir = Path(args.website_dir).resolve()
    else:
        website_dir = auto_detect_website_dir(repo)

    if website_dir:
        doc_paths.append(website_dir)
        docs_sub = website_dir / "docs"
        if docs_sub.exists():
            doc_paths.append(docs_sub)

    # ── Scan ──────────────────────────────────────────────────────
    mismatches = scan_docs(truth, doc_paths)

    # ── Output ────────────────────────────────────────────────────
    if args.json:
        # Serialise truth (skip large lists for readability)
        truth_out = {}
        for k, v in truth.items():
            if isinstance(v, list) and len(v) > 20:
                truth_out[k] = f"[{len(v)} items]"
            else:
                truth_out[k] = v

        result = {
            "ground_truth": truth_out,
            "mismatches": [m.to_dict() for m in mismatches],
            "total": len(mismatches),
            "fixable": sum(1 for m in mismatches if m.fixable),
            "manual": sum(1 for m in mismatches if not m.fixable),
        }
        print(json.dumps(result, indent=2, default=str))

    elif mismatches:
        print(f"\n{'=' * 64}")
        print(f"  Documentation Consistency Check: {len(mismatches)} mismatch(es)")
        print(f"{'=' * 64}\n")
        for m in mismatches:
            print(f"  {m}")
            ctx = m.context[:120]
            print(f"    ↳ {ctx}")
            print()

        fixable = sum(1 for m in mismatches if m.fixable)
        manual = len(mismatches) - fixable

        if args.fix:
            fixed = fix_docs(mismatches)
            print(f"  Auto-fixed {fixed} occurrence(s).")
            if manual:
                print(f"  {manual} mismatch(es) require manual review.")
        else:
            if fixable:
                print(f"  {fixable} fixable — run with --fix to auto-correct.")
            if manual:
                print(f"  {manual} require manual review.")
    else:
        print("Documentation consistency check PASSED — no mismatches found.")

    # Exit code: 0 = clean, 1 = mismatches present
    if mismatches and not args.fix:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
