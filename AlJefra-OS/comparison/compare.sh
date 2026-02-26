#!/bin/bash
#
# AlJefra OS AI — Experiment Comparison Tool
# Compares Experiment A (AI-Directed) vs Experiment B (GPU Binary Evolution)
#
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BASE_DIR="$SCRIPT_DIR/.."
EXP_A_LOG="$BASE_DIR/evolution/logs/evolution_log.jsonl"
EXP_B_LOG="$BASE_DIR/experiment_b/results/breakthroughs.jsonl"
EXP_B_GEN="$BASE_DIR/experiment_b/results/generations.jsonl"

echo "╔══════════════════════════════════════════════════════════╗"
echo "║    AlJefra OS AI — Evolution Experiment Comparison       ║"
echo "╚══════════════════════════════════════════════════════════╝"
echo ""

# ── Experiment A: AI-Directed ─────────────────────────────────────

echo "━━━ Experiment A: AI-Directed (Source-Level) ━━━"
if [ -f "$EXP_A_LOG" ]; then
    TOTAL_A=$(wc -l < "$EXP_A_LOG" 2>/dev/null || echo 0)
    BT_A=$(grep -c '"experiment":"A_ai_directed"\|"method":"source_optimization"' "$EXP_A_LOG" 2>/dev/null || echo 0)
    echo "  Total evolution events: $TOTAL_A"
    echo "  Breakthroughs:          $BT_A"

    # Show per-component results
    echo "  Per-component:"
    for comp in kernel memory smp network storage gpu_driver bus interrupts timer io syscalls vram_alloc cmd_queue dma scheduler; do
        count=$(grep -c "\"component\":\"$comp\"" "$EXP_A_LOG" 2>/dev/null || echo 0)
        if [ "$count" -gt 0 ]; then
            best=$(grep "\"component\":\"$comp\"" "$EXP_A_LOG" 2>/dev/null | \
                   grep -o '"improvement_pct":[0-9.]*' 2>/dev/null | \
                   cut -d: -f2 | sort -rn | head -1)
            echo "    $comp: $count events, best improvement: ${best:-N/A}%"
        fi
    done
else
    echo "  No results yet. Run Experiment A first."
    echo "  cd ../experiment_a && ./evolve_ai.sh"
fi

echo ""

# ── Experiment B: GPU Binary Evolution ────────────────────────────

echo "━━━ Experiment B: GPU Binary Evolution ━━━"
if [ -f "$EXP_B_LOG" ]; then
    TOTAL_B=$(wc -l < "$EXP_B_LOG" 2>/dev/null || echo 0)
    echo "  Total breakthroughs: $TOTAL_B"

    echo "  Per-component:"
    for comp in kernel memory smp network storage gpu_driver bus interrupts timer io syscalls vram_alloc cmd_queue dma scheduler; do
        count=$(grep -c "\"component\":\"$comp\"" "$EXP_B_LOG" 2>/dev/null || echo 0)
        if [ "$count" -gt 0 ]; then
            best=$(grep "\"component\":\"$comp\"" "$EXP_B_LOG" 2>/dev/null | \
                   grep -o '"improvement_pct":[0-9.]*' 2>/dev/null | \
                   cut -d: -f2 | sort -rn | head -1)
            echo "    $comp: $count breakthroughs, best improvement: ${best:-N/A}%"
        fi
    done
else
    echo "  No results yet. Run Experiment B first."
    echo "  cd ../experiment_b && ./evolve_binary.sh"
fi

if [ -f "$EXP_B_GEN" ]; then
    TOTAL_GENS=$(wc -l < "$EXP_B_GEN" 2>/dev/null || echo 0)
    echo "  Total generations evaluated: $TOTAL_GENS"
fi

echo ""

# ── Head-to-Head Comparison ───────────────────────────────────────

echo "━━━ Head-to-Head Comparison ━━━"

if [ -f "$EXP_A_LOG" ] && [ -f "$EXP_B_LOG" ]; then
    echo ""
    printf "  %-15s │ %-20s │ %-20s\n" "Component" "A (AI-Directed)" "B (Binary Evo)"
    printf "  %-15s │ %-20s │ %-20s\n" "───────────────" "────────────────────" "────────────────────"

    for comp in kernel memory smp network storage gpu_driver bus interrupts timer io syscalls vram_alloc cmd_queue dma scheduler; do
        best_a=$(grep "\"component\":\"$comp\"" "$EXP_A_LOG" 2>/dev/null | \
                 grep -o '"improvement_pct":[0-9.]*' 2>/dev/null | \
                 cut -d: -f2 | sort -rn | head -1)
        best_b=$(grep "\"component\":\"$comp\"" "$EXP_B_LOG" 2>/dev/null | \
                 grep -o '"improvement_pct":[0-9.]*' 2>/dev/null | \
                 cut -d: -f2 | sort -rn | head -1)

        if [ -n "$best_a" ] || [ -n "$best_b" ]; then
            a_str="${best_a:---}%"
            b_str="${best_b:---}%"

            # Determine winner
            winner=""
            if [ -n "$best_a" ] && [ -n "$best_b" ]; then
                if (( $(echo "$best_a > $best_b" | bc -l 2>/dev/null || echo 0) )); then
                    winner=" ← A wins"
                elif (( $(echo "$best_b > $best_a" | bc -l 2>/dev/null || echo 0) )); then
                    winner=" ← B wins"
                fi
            fi

            printf "  %-15s │ %+19s │ %+19s%s\n" "$comp" "$a_str" "$b_str" "$winner"
        fi
    done
    echo ""
else
    echo "  Run both experiments to see comparison."
fi

# ── Git Branches ──────────────────────────────────────────────────

echo "━━━ Evolution Branches ━━━"
cd "$BASE_DIR"
if git rev-parse --git-dir > /dev/null 2>&1; then
    BRANCHES=$(git branch -a 2>/dev/null | grep -c "binary-evo\|source-evo\|evolution" || echo 0)
    echo "  Evolution branches: $BRANCHES"
    git branch -a 2>/dev/null | grep "binary-evo\|source-evo\|evolution" | head -10 | while read -r branch; do
        echo "    $branch"
    done
    if [ "$BRANCHES" -gt 10 ]; then
        echo "    ... and $((BRANCHES - 10)) more"
    fi
else
    echo "  Not a git repository"
fi

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
