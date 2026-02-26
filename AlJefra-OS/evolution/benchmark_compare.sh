#!/usr/bin/env bash
# =============================================================================
# Enhanced Benchmark Comparison — AlJefra OS AI
# Parameterized: [base_commit] [target_commit] [base_label] [target_label]
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
GIT_DIR="$(cd "$REPO_ROOT/.." && git rev-parse --show-toplevel)"

# ─────────────────────────────────────────────────────────────────────────────
# Parameterization (defaults: fd5a707 vs HEAD)
# ─────────────────────────────────────────────────────────────────────────────
BASE_COMMIT="${1:-fd5a707}"
TARGET_COMMIT="${2:-HEAD}"
BASE_LABEL="${3:-BASELINE}"
TARGET_LABEL="${4:-GEN10}"

# Resolve short hashes
BASE_SHORT="$(git -C "$GIT_DIR" rev-parse --short "$BASE_COMMIT")"
TARGET_SHORT="$(git -C "$GIT_DIR" rev-parse --short "$TARGET_COMMIT")"

REPORT="$SCRIPT_DIR/logs/BENCHMARK_${BASE_LABEL}_VS_${TARGET_LABEL}.md"

# Colors for terminal output
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; NC='\033[0m'

info()  { echo -e "${CYAN}[INFO]${NC}  $*"; }
ok()    { echo -e "${GREEN}[OK]${NC}    $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
fail()  { echo -e "${RED}[FAIL]${NC}  $*"; exit 1; }

info "Comparing: $BASE_LABEL ($BASE_SHORT) vs $TARGET_LABEL ($TARGET_SHORT)"

# Temp directories (cleaned on exit)
BASE_DIR=""
TARGET_DIR=""
cleanup() {
    [[ -n "$BASE_DIR" && -d "$BASE_DIR" ]] && rm -rf "$BASE_DIR"
    [[ -n "$TARGET_DIR" && -d "$TARGET_DIR" ]] && rm -rf "$TARGET_DIR"
}
trap cleanup EXIT

# ─────────────────────────────────────────────────────────────────────────────
# Verify tools
# ─────────────────────────────────────────────────────────────────────────────
command -v nasm >/dev/null 2>&1 || fail "nasm not found"
command -v git  >/dev/null 2>&1 || fail "git not found"
command -v awk  >/dev/null 2>&1 || fail "awk not found"

mkdir -p "$SCRIPT_DIR/logs"

# ─────────────────────────────────────────────────────────────────────────────
# Step 1: Extract & Build Base Kernel
# ─────────────────────────────────────────────────────────────────────────────
info "Step 1: Building base kernel ($BASE_SHORT)..."
BASE_DIR=$(mktemp -d)
cd "$GIT_DIR"
git archive "$BASE_COMMIT" -- AlJefra-OS/aljefra/ | tar -C "$BASE_DIR" -x

BASE_SRC="$BASE_DIR/AlJefra-OS/aljefra/src"
BASE_BIN="$BASE_DIR/AlJefra-OS/aljefra/bin"
mkdir -p "$BASE_BIN"

cd "$BASE_SRC"
# Use -dNO_GPU only if target has it (baseline may not have GPU code)
BASE_NASM_FLAGS="-dNO_VGA"
if grep -q 'NO_GPU' "$BASE_SRC/drivers.asm" 2>/dev/null; then
    BASE_NASM_FLAGS="$BASE_NASM_FLAGS -dNO_GPU"
fi
nasm $BASE_NASM_FLAGS kernel.asm -o "$BASE_BIN/kernel.sys" -l "$BASE_BIN/kernel-debug.txt" \
    || fail "Base kernel build failed"
ok "Base kernel built: $(wc -c < "$BASE_BIN/kernel.sys") bytes"

# ─────────────────────────────────────────────────────────────────────────────
# Step 2: Build Target Kernel with Listing
# ─────────────────────────────────────────────────────────────────────────────
info "Step 2: Building target kernel ($TARGET_SHORT)..."
TARGET_DIR=$(mktemp -d)
cd "$GIT_DIR"
git archive "$TARGET_COMMIT" -- AlJefra-OS/aljefra/ | tar -C "$TARGET_DIR" -x

TARGET_SRC="$TARGET_DIR/AlJefra-OS/aljefra/src"
TARGET_BIN="$TARGET_DIR/AlJefra-OS/aljefra/bin"
mkdir -p "$TARGET_BIN"

cd "$TARGET_SRC"
nasm -dNO_VGA -dNO_GPU kernel.asm -o "$TARGET_BIN/kernel.sys" -l "$TARGET_BIN/kernel-debug.txt" \
    || fail "Target kernel build failed"
ok "Target kernel built: $(wc -c < "$TARGET_BIN/kernel.sys") bytes"

# ─────────────────────────────────────────────────────────────────────────────
# Analysis Functions
# ─────────────────────────────────────────────────────────────────────────────

count_instructions() {
    local listing="$1"
    awk '{
        n = split($0, f)
        if (n >= 3 && f[2] ~ /^[0-9A-Fa-f]{8}$/ && f[3] ~ /^[0-9A-Fa-f]/) count++
    } END { print count+0 }' "$listing"
}

count_code_bytes() {
    local listing="$1"
    awk '{
        n = split($0, f)
        if (n >= 3 && f[2] ~ /^[0-9A-Fa-f]{8}$/ && f[3] ~ /^[0-9A-Fa-f]/) {
            hex = f[3]
            if (match(hex, /^([0-9A-Fa-f]+)<rep ([0-9A-Fa-f]+)h>$/, m)) {
                total += (length(m[1]) / 2) * strtonum("0x" m[2])
            } else {
                gsub(/[\[\]()]/, "", hex)
                total += length(hex) / 2
            }
        }
    } END { print int(total) }' "$listing"
}

count_pattern() {
    local src_dir="$1" pattern="$2"
    local result
    result=$(grep -rci "$pattern" "$src_dir" --include='*.asm' 2>/dev/null || true)
    echo "$result" | awk -F: '{s+=$2} END {print s+0}'
}

count_pattern_E() {
    local src_dir="$1" pattern="$2"
    local result
    result=$(grep -rcE "$pattern" "$src_dir" --include='*.asm' 2>/dev/null || true)
    echo "$result" | awk -F: '{s+=$2} END {print s+0}'
}

count_lines_matching() {
    local src_dir="$1" pattern="$2"
    local result
    result=$(grep -rn "$pattern" "$src_dir" --include='*.asm' 2>/dev/null || true)
    echo "$result" | grep -v '^$' | wc -l
}

per_component_stats() {
    local listing="$1"
    awk '
    BEGIN { current = "kernel.asm"; inst = 0; bytes = 0 }
    /%include/ {
        if (inst > 0 || bytes > 0) printf "%s %d %d\n", current, inst, bytes
        if (match($0, /"([^"]+)"/, arr)) current = arr[1]
        inst = 0; bytes = 0; next
    }
    {
        n = split($0, f)
        if (n >= 3 && f[2] ~ /^[0-9A-Fa-f]{8}$/ && f[3] ~ /^[0-9A-Fa-f]/) {
            inst++
            hex = f[3]
            if (match(hex, /^([0-9A-Fa-f]+)<rep ([0-9A-Fa-f]+)h>$/, m)) {
                bytes += (length(m[1]) / 2) * strtonum("0x" m[2])
            } else {
                gsub(/[\[\]()]/, "", hex)
                bytes += length(hex) / 2
            }
        }
    }
    END { if (inst > 0 || bytes > 0) printf "%s %d %d\n", current, inst, bytes }
    ' "$listing"
}

count_src_lines() {
    local src_dir="$1" file="$2"
    if [[ -f "$src_dir/$file" ]]; then wc -l < "$src_dir/$file"; else echo 0; fi
}

extract_function() {
    local listing="$1" func_label="$2"
    local func_prefix="$func_label"
    awk -v label="$func_label:" -v prefix="$func_prefix" '
    BEGIN { found = 0 }
    {
        if (!found) {
            if (index($0, label) > 0) {
                line = $0
                if (match(line, /[;]/) && match(line, label) > RSTART) next
                if (match(line, "(^|[^a-zA-Z0-9_])" label, arr)) { found = 1; print; next }
            }
            next
        }
        n = split($0, f)
        if (n >= 2) {
            src_start = 2
            if (f[src_start] ~ /^[0-9A-Fa-f]{8}$/) {
                src_start++
                if (src_start <= n && f[src_start] ~ /^[0-9A-Fa-f]/) src_start++
            }
            while (src_start <= n && f[src_start] ~ /^<[0-9]+>$/) src_start++
            if (src_start <= n) {
                src_token = f[src_start]
                if (src_token ~ /^[a-zA-Z_][a-zA-Z0-9_.]*:$/ && src_token != label) {
                    if (index(src_token, prefix) != 1) exit
                }
            }
        }
        print
    }' "$listing"
}

func_instr_count() {
    awk '{ n = split($0, f); if (n >= 3 && f[2] ~ /^[0-9A-Fa-f]{8}$/ && f[3] ~ /^[0-9A-Fa-f]/) c++ } END { print c+0 }'
}

func_byte_count() {
    awk '{
        n = split($0, f)
        if (n >= 3 && f[2] ~ /^[0-9A-Fa-f]{8}$/ && f[3] ~ /^[0-9A-Fa-f]/) {
            hex = f[3]
            if (match(hex, /^([0-9A-Fa-f]+)<rep ([0-9A-Fa-f]+)h>$/, m)) {
                b += (length(m[1]) / 2) * strtonum("0x" m[2])
            } else { gsub(/[\[\]()]/, "", hex); b += length(hex) / 2 }
        }
    } END { print int(b) }'
}

# Safe percentage calculation
pct() {
    local base=$1 delta=$2
    if [[ $base -gt 0 ]]; then
        awk "BEGIN { printf \"%.1f%%\", ($delta / $base) * 100 }"
    else
        echo "N/A"
    fi
}

fmt_delta() {
    local base=$1 target=$2
    local d=$((target - base))
    if [[ $d -gt 0 ]]; then echo "+$d"
    elif [[ $d -lt 0 ]]; then echo "$d"
    else echo "0"
    fi
}

# ─────────────────────────────────────────────────────────────────────────────
# Step 3: Binary + Listing Analysis
# ─────────────────────────────────────────────────────────────────────────────
info "Step 3: Analyzing binary sizes and listings..."
BASE_SIZE=$(wc -c < "$BASE_BIN/kernel.sys")
TARGET_SIZE=$(wc -c < "$TARGET_BIN/kernel.sys")
SIZE_DELTA=$((TARGET_SIZE - BASE_SIZE))
SIZE_PCT=$(pct "$BASE_SIZE" "$SIZE_DELTA")

BASE_INSTR=$(count_instructions "$BASE_BIN/kernel-debug.txt")
TARGET_INSTR=$(count_instructions "$TARGET_BIN/kernel-debug.txt")
INSTR_DELTA=$((TARGET_INSTR - BASE_INSTR))

BASE_CODE_BYTES=$(count_code_bytes "$BASE_BIN/kernel-debug.txt")
TARGET_CODE_BYTES=$(count_code_bytes "$TARGET_BIN/kernel-debug.txt")
CODE_DELTA=$((TARGET_CODE_BYTES - BASE_CODE_BYTES))

# Code density: average bytes per instruction
if [[ $BASE_INSTR -gt 0 ]]; then
    BASE_DENSITY=$(awk "BEGIN { printf \"%.3f\", $BASE_CODE_BYTES / $BASE_INSTR }")
    TARGET_DENSITY=$(awk "BEGIN { printf \"%.3f\", $TARGET_CODE_BYTES / $TARGET_INSTR }")
else
    BASE_DENSITY="N/A"; TARGET_DENSITY="N/A"
fi

ok "Instructions: base=$BASE_INSTR, target=$TARGET_INSTR (delta=$INSTR_DELTA)"
ok "Code bytes: base=$BASE_CODE_BYTES, target=$TARGET_CODE_BYTES (delta=$CODE_DELTA)"
ok "Code density: base=${BASE_DENSITY} B/I, target=${TARGET_DENSITY} B/I"

# ─────────────────────────────────────────────────────────────────────────────
# Step 4: Optimization Pattern Counts
# ─────────────────────────────────────────────────────────────────────────────
info "Step 4: Counting optimization patterns..."

declare -A PAT_BASE PAT_TARGET

PAT_BASE[test_instr]=$(count_pattern "$BASE_SRC" 'test ')
PAT_TARGET[test_instr]=$(count_pattern "$TARGET_SRC" 'test ')
PAT_BASE[cmp_zero]=$(count_lines_matching "$BASE_SRC" 'cmp.*,[ ]*0$\|cmp.*,[ ]*0 ')
PAT_TARGET[cmp_zero]=$(count_lines_matching "$TARGET_SRC" 'cmp.*,[ ]*0$\|cmp.*,[ ]*0 ')
PAT_BASE[lea]=$(count_pattern "$BASE_SRC" 'lea ')
PAT_TARGET[lea]=$(count_pattern "$TARGET_SRC" 'lea ')
PAT_BASE[pause]=$(count_pattern_E "$BASE_SRC" '	pause( |$)')
PAT_TARGET[pause]=$(count_pattern_E "$TARGET_SRC" '	pause( |$)')
PAT_BASE[bt_ops]=$(count_pattern_E "$BASE_SRC" '	bt[sr]? ')
PAT_TARGET[bt_ops]=$(count_pattern_E "$TARGET_SRC" '	bt[sr]? ')
PAT_BASE[jmp]=$(count_pattern "$BASE_SRC" 'jmp ')
PAT_TARGET[jmp]=$(count_pattern "$TARGET_SRC" 'jmp ')
PAT_BASE[prefetch]=$(count_pattern_E "$BASE_SRC" 'prefetch(nta|t[012])')
PAT_TARGET[prefetch]=$(count_pattern_E "$TARGET_SRC" 'prefetch(nta|t[012])')
PAT_BASE[xchg]=$(count_pattern "$BASE_SRC" 'xchg ')
PAT_TARGET[xchg]=$(count_pattern "$TARGET_SRC" 'xchg ')
PAT_BASE[rep_stosq]=$(count_pattern "$BASE_SRC" 'rep stosq')
PAT_TARGET[rep_stosq]=$(count_pattern "$TARGET_SRC" 'rep stosq')
PAT_BASE[rep_stosd]=$(count_pattern "$BASE_SRC" 'rep stosd')
PAT_TARGET[rep_stosd]=$(count_pattern "$TARGET_SRC" 'rep stosd')
PAT_BASE[call_apic]=$(count_pattern "$BASE_SRC" 'call os_apic')
PAT_TARGET[call_apic]=$(count_pattern "$TARGET_SRC" 'call os_apic')
PAT_BASE[imul]=$(count_pattern "$BASE_SRC" 'imul ')
PAT_TARGET[imul]=$(count_pattern "$TARGET_SRC" 'imul ')
PAT_BASE[mul]=$(count_pattern_E "$BASE_SRC" '	mul [a-z]')
PAT_TARGET[mul]=$(count_pattern_E "$TARGET_SRC" '	mul [a-z]')
PAT_BASE[movzx]=$(count_pattern "$BASE_SRC" 'movzx')
PAT_TARGET[movzx]=$(count_pattern "$TARGET_SRC" 'movzx')

ok "Pattern counts collected"

# ─────────────────────────────────────────────────────────────────────────────
# Step 5: Per-Component Comparison
# ─────────────────────────────────────────────────────────────────────────────
info "Step 5: Per-component analysis..."

SHARED_FILES=(
    "kernel.asm" "init.asm" "init/64.asm" "init/bus.asm" "init/hid.asm"
    "init/net.asm" "init/nvs.asm" "init/sys.asm" "syscalls.asm"
    "syscalls/bus.asm" "syscalls/debug.asm" "syscalls/io.asm"
    "syscalls/net.asm" "syscalls/nvs.asm" "syscalls/smp.asm"
    "syscalls/system.asm" "drivers.asm" "drivers/apic.asm"
    "drivers/ioapic.asm" "drivers/msi.asm" "drivers/ps2.asm"
    "drivers/serial.asm" "drivers/timer.asm" "drivers/virtio.asm"
    "drivers/bus/pci.asm" "drivers/bus/pcie.asm"
    "drivers/nvs/virtio-blk.asm" "drivers/net/virtio-net.asm"
    "drivers/lfb/lfb.asm" "interrupt.asm" "sysvar.asm"
)

BASE_COMP=$(per_component_stats "$BASE_BIN/kernel-debug.txt")
TARGET_COMP=$(per_component_stats "$TARGET_BIN/kernel-debug.txt")

declare -A BASE_COMP_INSTR BASE_COMP_BYTES TARGET_COMP_INSTR TARGET_COMP_BYTES
while IFS=' ' read -r name instr bytes; do
    [[ -z "$name" ]] && continue
    BASE_COMP_INSTR["$name"]=$instr
    BASE_COMP_BYTES["$name"]=$bytes
done <<< "$BASE_COMP"
while IFS=' ' read -r name instr bytes; do
    [[ -z "$name" ]] && continue
    TARGET_COMP_INSTR["$name"]=$instr
    TARGET_COMP_BYTES["$name"]=$bytes
done <<< "$TARGET_COMP"

ok "Per-component analysis complete"

# ─────────────────────────────────────────────────────────────────────────────
# Step 6: Hot-Path Deep Analysis (expanded to 10 functions)
# ─────────────────────────────────────────────────────────────────────────────
info "Step 6: Hot-path analysis..."

HOT_PATHS=(
    "ap_clear" "b_smp_lock" "b_smp_unlock" "ap_wakeup" "b_smp_wakeup"
    "b_smp_get_id" "b_smp_wakeup_all" "b_smp_reset" "lfb_draw_line"
    "hpet_ns"
)

declare -A HP_BASE_INSTR HP_BASE_BYTES HP_TARGET_INSTR HP_TARGET_BYTES
for func in "${HOT_PATHS[@]}"; do
    base_func=$(extract_function "$BASE_BIN/kernel-debug.txt" "$func")
    target_func=$(extract_function "$TARGET_BIN/kernel-debug.txt" "$func")
    HP_BASE_INSTR[$func]=$(echo "$base_func" | func_instr_count)
    HP_TARGET_INSTR[$func]=$(echo "$target_func" | func_instr_count)
    HP_BASE_BYTES[$func]=$(echo "$base_func" | func_byte_count)
    HP_TARGET_BYTES[$func]=$(echo "$target_func" | func_byte_count)
done
ok "Hot-path analysis complete"

# ─────────────────────────────────────────────────────────────────────────────
# Step 7: QEMU Boot Timing (best-effort)
# ─────────────────────────────────────────────────────────────────────────────
BOOT_BASE_MS="N/A"; BOOT_TARGET_MS="N/A"; BOOT_DELTA_MS="N/A"

qemu_boot_time() {
    local kernel_bin="$1" label="$2"
    local serial_log; serial_log=$(mktemp)
    local pure64=""
    for candidate in "$REPO_ROOT/src/Pure64/bin/software-bios.sys" "$REPO_ROOT/Pure64/bin/software-bios.sys"; do
        [[ -f "$candidate" ]] && { pure64="$candidate"; break; }
    done
    if [[ -z "$pure64" ]]; then
        warn "Pure64 bootloader not found, skipping QEMU boot timing for $label" >&2
        rm -f "$serial_log"; echo "N/A"; return
    fi
    local boot_img; boot_img=$(mktemp)
    dd if=/dev/zero of="$boot_img" bs=1M count=4 2>/dev/null
    dd if="$pure64" of="$boot_img" bs=512 seek=16 conv=notrunc 2>/dev/null
    dd if="$kernel_bin" of="$boot_img" bs=512 seek=48 conv=notrunc 2>/dev/null
    local start_ns end_ns; start_ns=$(date +%s%N)
    timeout 10 qemu-system-x86_64 -machine q35 -smp 1 -cpu Westmere -m 256 \
        -display none -no-reboot -drive file="$boot_img",format=raw,if=virtio \
        -serial file:"$serial_log" -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
        2>/dev/null || true
    end_ns=$(date +%s%N)
    local elapsed_ms=$(( (end_ns - start_ns) / 1000000 ))
    if [[ -s "$serial_log" ]]; then echo "$elapsed_ms"; else echo "N/A"; fi
    rm -f "$serial_log" "$boot_img"
}

if command -v qemu-system-x86_64 >/dev/null 2>&1; then
    info "Step 7: QEMU boot timing..."
    BOOT_BASE_MS=$(qemu_boot_time "$BASE_BIN/kernel.sys" "$BASE_LABEL")
    BOOT_TARGET_MS=$(qemu_boot_time "$TARGET_BIN/kernel.sys" "$TARGET_LABEL")
    if [[ "$BOOT_BASE_MS" != "N/A" && "$BOOT_TARGET_MS" != "N/A" ]]; then
        BOOT_DELTA_MS=$((BOOT_TARGET_MS - BOOT_BASE_MS))
    fi
else
    warn "qemu-system-x86_64 not found, skipping boot timing"
fi

# ─────────────────────────────────────────────────────────────────────────────
# Step 8: Generate Markdown Report
# ─────────────────────────────────────────────────────────────────────────────
info "Step 8: Generating report..."

NOW=$(date -u '+%Y-%m-%d %H:%M UTC')
NASM_VER=$(nasm --version 2>/dev/null | head -1)

cat > "$REPORT" << HEADER
# Benchmark: $BASE_LABEL vs $TARGET_LABEL

**Generated:** $NOW
**Tool:** benchmark_compare.sh | $NASM_VER
**Base:** commit \`$BASE_SHORT\` ($BASE_LABEL)
**Target:** commit \`$TARGET_SHORT\` ($TARGET_LABEL)

> Both kernels built with \`-dNO_VGA\`. Target additionally uses \`-dNO_GPU\` to exclude
> GPU/evolution/AI code added post-fork. Comparison covers **shared original components only**.

---

## 1. Summary

| Metric | $BASE_LABEL | $TARGET_LABEL | Delta | Change |
|--------|-------------|---------------|-------|--------|
| **Binary size** (bytes) | $BASE_SIZE | $TARGET_SIZE | $(fmt_delta $BASE_SIZE $TARGET_SIZE) | $SIZE_PCT |
| **Total instructions** | $BASE_INSTR | $TARGET_INSTR | $(fmt_delta $BASE_INSTR $TARGET_INSTR) | $(pct $BASE_INSTR $INSTR_DELTA) |
| **Code bytes** | $BASE_CODE_BYTES | $TARGET_CODE_BYTES | $(fmt_delta $BASE_CODE_BYTES $TARGET_CODE_BYTES) | $(pct $BASE_CODE_BYTES $CODE_DELTA) |
| **Code density** (B/instr) | $BASE_DENSITY | $TARGET_DENSITY | — | — |
| **Boot time (QEMU)** | ${BOOT_BASE_MS}ms | ${BOOT_TARGET_MS}ms | ${BOOT_DELTA_MS}ms | — |

HEADER

# ─────────────────────────────────────────────────────────────────────────────
# Section 2: Optimization Pattern Counts
# ─────────────────────────────────────────────────────────────────────────────
cat >> "$REPORT" << PATTERNS_HEADER
---

## 2. Optimization Pattern Counts

| Pattern | Description | $BASE_LABEL | $TARGET_LABEL | Delta |
|---------|-------------|-------------|---------------|-------|
PATTERNS_HEADER

{
echo "| \`test\` | Zero-comparison (efficient) | ${PAT_BASE[test_instr]} | ${PAT_TARGET[test_instr]} | $(fmt_delta ${PAT_BASE[test_instr]} ${PAT_TARGET[test_instr]}) |"
echo "| \`cmp *, 0\` | Zero-comparison (replaced by test) | ${PAT_BASE[cmp_zero]} | ${PAT_TARGET[cmp_zero]} | $(fmt_delta ${PAT_BASE[cmp_zero]} ${PAT_TARGET[cmp_zero]}) |"
echo "| \`lea\` | Address calc (replaces shl+add) | ${PAT_BASE[lea]} | ${PAT_TARGET[lea]} | $(fmt_delta ${PAT_BASE[lea]} ${PAT_TARGET[lea]}) |"
echo "| \`pause\` | Spin-wait hint | ${PAT_BASE[pause]} | ${PAT_TARGET[pause]} | $(fmt_delta ${PAT_BASE[pause]} ${PAT_TARGET[pause]}) |"
echo "| \`bt/bts/btr\` | Bit-test operations | ${PAT_BASE[bt_ops]} | ${PAT_TARGET[bt_ops]} | $(fmt_delta ${PAT_BASE[bt_ops]} ${PAT_TARGET[bt_ops]}) |"
echo "| \`imul\` | Signed multiply (no EDX clobber) | ${PAT_BASE[imul]} | ${PAT_TARGET[imul]} | $(fmt_delta ${PAT_BASE[imul]} ${PAT_TARGET[imul]}) |"
echo "| \`mul\` | Unsigned multiply (EDX clobbered) | ${PAT_BASE[mul]} | ${PAT_TARGET[mul]} | $(fmt_delta ${PAT_BASE[mul]} ${PAT_TARGET[mul]}) |"
echo "| \`movzx\` | Zero-extending loads | ${PAT_BASE[movzx]} | ${PAT_TARGET[movzx]} | $(fmt_delta ${PAT_BASE[movzx]} ${PAT_TARGET[movzx]}) |"
echo "| \`call os_apic_*\` | APIC calls (inlined in Gen-10) | ${PAT_BASE[call_apic]} | ${PAT_TARGET[call_apic]} | $(fmt_delta ${PAT_BASE[call_apic]} ${PAT_TARGET[call_apic]}) |"
echo "| \`prefetchnta\` | Cache prefetch hints | ${PAT_BASE[prefetch]} | ${PAT_TARGET[prefetch]} | $(fmt_delta ${PAT_BASE[prefetch]} ${PAT_TARGET[prefetch]}) |"
echo "| \`rep stosq\` | 64-bit memory fill | ${PAT_BASE[rep_stosq]} | ${PAT_TARGET[rep_stosq]} | $(fmt_delta ${PAT_BASE[rep_stosq]} ${PAT_TARGET[rep_stosq]}) |"
echo "| \`xchg\` | Implicit LOCK exchange | ${PAT_BASE[xchg]} | ${PAT_TARGET[xchg]} | $(fmt_delta ${PAT_BASE[xchg]} ${PAT_TARGET[xchg]}) |"
} >> "$REPORT"

# ─────────────────────────────────────────────────────────────────────────────
# Section 3: Per-Component Comparison (with IC% and CB%)
# ─────────────────────────────────────────────────────────────────────────────
cat >> "$REPORT" << 'COMP_HEADER'

---

## 3. Per-Component Comparison

| Component | IC (base) | IC (target) | ΔIC | IC% | CB (base) | CB (target) | ΔCB | CB% |
|-----------|-----------|-------------|-----|-----|-----------|-------------|-----|-----|
COMP_HEADER

TOTAL_BASE_IC=0; TOTAL_TARGET_IC=0
TOTAL_BASE_CB=0; TOTAL_TARGET_CB=0

for file in "${SHARED_FILES[@]}"; do
    base_ic=${BASE_COMP_INSTR[$file]:-0}
    target_ic=${TARGET_COMP_INSTR[$file]:-0}
    d_ic=$((target_ic - base_ic))

    base_cb=${BASE_COMP_BYTES[$file]:-0}
    target_cb=${TARGET_COMP_BYTES[$file]:-0}
    d_cb=$((target_cb - base_cb))

    TOTAL_BASE_IC=$((TOTAL_BASE_IC + base_ic))
    TOTAL_TARGET_IC=$((TOTAL_TARGET_IC + target_ic))
    TOTAL_BASE_CB=$((TOTAL_BASE_CB + base_cb))
    TOTAL_TARGET_CB=$((TOTAL_TARGET_CB + target_cb))

    ic_pct=$(pct "$base_ic" "$d_ic")
    cb_pct=$(pct "$base_cb" "$d_cb")

    d_ic_s=$(fmt_delta "$base_ic" "$target_ic")
    d_cb_s=$(fmt_delta "$base_cb" "$target_cb")

    echo "| \`$file\` | $base_ic | $target_ic | $d_ic_s | $ic_pct | $base_cb | $target_cb | $d_cb_s | $cb_pct |" >> "$REPORT"
done

D_TOTAL_IC=$((TOTAL_TARGET_IC - TOTAL_BASE_IC))
D_TOTAL_CB=$((TOTAL_TARGET_CB - TOTAL_BASE_CB))
echo "| **TOTAL** | **$TOTAL_BASE_IC** | **$TOTAL_TARGET_IC** | **$(fmt_delta $TOTAL_BASE_IC $TOTAL_TARGET_IC)** | **$(pct $TOTAL_BASE_IC $D_TOTAL_IC)** | **$TOTAL_BASE_CB** | **$TOTAL_TARGET_CB** | **$(fmt_delta $TOTAL_BASE_CB $TOTAL_TARGET_CB)** | **$(pct $TOTAL_BASE_CB $D_TOTAL_CB)** |" >> "$REPORT"

# ─────────────────────────────────────────────────────────────────────────────
# Section 4: Hot-Path Function Analysis (top 10)
# ─────────────────────────────────────────────────────────────────────────────
cat >> "$REPORT" << 'HP_HEADER'

---

## 4. Hot-Path Function Analysis

| Function | IC (base) | IC (target) | ΔIC | IC% | Bytes (base) | Bytes (target) | ΔBytes | B% |
|----------|-----------|-------------|-----|-----|-------------|----------------|--------|-----|
HP_HEADER

for func in "${HOT_PATHS[@]}"; do
    bi=${HP_BASE_INSTR[$func]:-0}
    ti=${HP_TARGET_INSTR[$func]:-0}
    di=$((ti - bi))
    bb=${HP_BASE_BYTES[$func]:-0}
    tb=${HP_TARGET_BYTES[$func]:-0}
    db=$((tb - bb))
    echo "| \`$func\` | $bi | $ti | $(fmt_delta $bi $ti) | $(pct $bi $di) | $bb | $tb | $(fmt_delta $bb $tb) | $(pct $bb $db) |" >> "$REPORT"
done

# ─────────────────────────────────────────────────────────────────────────────
# Section 5: Optimization Category Impact
# ─────────────────────────────────────────────────────────────────────────────
cat >> "$REPORT" << 'CAT_HEADER'

---

## 5. Optimization Categories

| Category | Description | Key Changes |
|----------|-------------|-------------|
| **Peephole** | Instruction substitution (test/cmp, inc/add, imul/mul) | `mul ecx` → `imul eax, ecx`, `xor+mov` → `movzx` |
| **Dead code** | Removing unused instructions | Dead `push/pop rcx` in interrupt handlers, dead `xor edx` |
| **Inlining** | Eliminating call/ret overhead | APIC read/write inlined in `b_smp_*` functions |
| **Loop restructure** | Top-test → bottom-test | `hpet_wake_idle_core` scan loop |
| **Strength reduction** | Replacing expensive ops with cheaper ones | `mul ecx, 4` → `shl eax, 2`, indexed addressing in `hpet_write` |
| **Register save elim** | Removing unnecessary push/pop pairs | `push/pop rdx` removed from `lfb_pixel`, `lfb_draw_line`, `lfb_update_cursor` |
CAT_HEADER

# ─────────────────────────────────────────────────────────────────────────────
# Section 6: Generation History (from evolution_log.jsonl)
# ─────────────────────────────────────────────────────────────────────────────
EVOLOG="$SCRIPT_DIR/logs/evolution_log.jsonl"

cat >> "$REPORT" << 'HIST_HEADER'

---

## 6. Evolution History

HIST_HEADER

if [[ -f "$EVOLOG" ]]; then
    # Parse generation history: find unique generations and their descriptions
    echo "| Gen | Component | Description |" >> "$REPORT"
    echo "|-----|-----------|-------------|" >> "$REPORT"

    awk -F'"' '{
        gen=""; comp=""; desc=""
        for (i=1; i<=NF; i++) {
            if ($i ~ /generation/) {
                val=$(i+1)
                gsub(/[^0-9]/, "", val)
                gen=val
            }
            if ($i ~ /component/) comp=$(i+2)
            if ($i ~ /description/) desc=$(i+2)
        }
        if (gen != "" && comp != "" && desc != "") {
            printf "| %s | %s | %s |\n", gen, comp, desc
        }
    }' "$EVOLOG" >> "$REPORT"

    # Cumulative instruction count per generation (if we have baseline data)
    echo "" >> "$REPORT"
    echo "**Cumulative from baseline ($BASE_SHORT):** $BASE_INSTR instructions, $BASE_CODE_BYTES code bytes" >> "$REPORT"
    echo "**Current ($TARGET_SHORT):** $TARGET_INSTR instructions, $TARGET_CODE_BYTES code bytes" >> "$REPORT"
    echo "**Net reduction:** $(fmt_delta $BASE_INSTR $TARGET_INSTR) instructions ($(pct $BASE_INSTR $INSTR_DELTA)), $(fmt_delta $BASE_CODE_BYTES $TARGET_CODE_BYTES) code bytes ($(pct $BASE_CODE_BYTES $CODE_DELTA))" >> "$REPORT"
else
    echo "*No evolution_log.jsonl found*" >> "$REPORT"
fi

# ─────────────────────────────────────────────────────────────────────────────
# Section 7: Methodology
# ─────────────────────────────────────────────────────────────────────────────
cat >> "$REPORT" << FOOTER

---

## 7. Methodology

- **Static analysis** via NASM listing files (\`-l\` flag) — maps every source line to its binary encoding
- **Pattern counting** via grep across shared \`.asm\` source files
- **Per-component tracking** by parsing \`%include\` boundaries in NASM listings
- **Code density** = total code bytes / total instructions (lower = more efficient encoding)
- **Fair comparison**: Both kernels built with \`-dNO_VGA\`; target with \`-dNO_GPU\` to exclude post-fork additions
- **Shared files**: ${#SHARED_FILES[@]} components present in both base and target
- **QEMU timing**: Wall-clock boot to serial output (if bootloader available)

---

*Report generated by \`evolution/benchmark_compare.sh\`*
FOOTER

ok "Report written to: $REPORT"
echo ""
info "════════════════════════════════════════════════"
info "  Benchmark complete!"
info "  Report: $REPORT"
info "════════════════════════════════════════════════"
