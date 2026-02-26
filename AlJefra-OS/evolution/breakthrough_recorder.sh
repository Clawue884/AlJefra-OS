#!/bin/bash
# =============================================================================
# AlJefra OS -- Breakthrough Recorder
# Copyright (C) 2026
#
# This script is called by the evolution engine when a breakthrough is detected.
# It creates a git branch/tag, records metrics, and maintains the evolution log.
#
# Usage: ./breakthrough_recorder.sh <component> <generation> <improvement_pct> <description>
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
LOG_DIR="$SCRIPT_DIR/logs"
BENCHMARK_DIR="$SCRIPT_DIR/benchmarks"

# Arguments
COMPONENT="${1:-unknown}"
GENERATION="${2:-0}"
IMPROVEMENT="${3:-0}"
DESCRIPTION="${4:-No description}"

# Timestamp
TIMESTAMP=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
DATE_SHORT=$(date -u +"%Y%m%d-%H%M%S")

# Branch name
BRANCH_NAME="evo-${COMPONENT}-gen${GENERATION}-${DATE_SHORT}"

# Ensure directories exist
mkdir -p "$LOG_DIR" "$BENCHMARK_DIR"

# =============================================================================
# Step 1: Record the breakthrough in the evolution log
# =============================================================================

EVOLUTION_LOG="$LOG_DIR/evolution_log.jsonl"

cat >> "$EVOLUTION_LOG" << LOGEOF
{"timestamp":"${TIMESTAMP}","component":"${COMPONENT}","generation":${GENERATION},"improvement_pct":${IMPROVEMENT},"description":"${DESCRIPTION}","branch":"${BRANCH_NAME}"}
LOGEOF

echo "[BREAKTHROUGH] ${TIMESTAMP} | ${COMPONENT} | Gen ${GENERATION} | +${IMPROVEMENT}% | ${DESCRIPTION}"

# =============================================================================
# Step 2: Record benchmark snapshot
# =============================================================================

BENCHMARK_FILE="$BENCHMARK_DIR/${BRANCH_NAME}.json"

cat > "$BENCHMARK_FILE" << BENCHEOF
{
  "timestamp": "${TIMESTAMP}",
  "component": "${COMPONENT}",
  "generation": ${GENERATION},
  "improvement_pct": ${IMPROVEMENT},
  "description": "${DESCRIPTION}",
  "branch": "${BRANCH_NAME}",
  "metrics": {
    "kernel_latency": 0,
    "gpu_latency": 0,
    "memory_bandwidth": 0,
    "smp_efficiency": 0,
    "net_latency": 0,
    "storage_latency": 0
  },
  "system_info": {
    "gpu": "NVIDIA RTX 5090 (GB202)",
    "gpu_vram": "32GB GDDR7",
    "architecture": "x86-64 AlJefra OS Exokernel"
  }
}
BENCHEOF

# =============================================================================
# Step 3: Create a git branch for this breakthrough
# =============================================================================

cd "$PROJECT_ROOT"

# Only create branch if we're in a git repo
if git rev-parse --is-inside-work-tree > /dev/null 2>&1; then
    # Stage all changes
    git add -A

    # Create commit for this breakthrough
    git commit -m "$(cat <<COMMITEOF
[BREAKTHROUGH] ${COMPONENT}: +${IMPROVEMENT}% improvement (Gen ${GENERATION})

${DESCRIPTION}

Evolution Engine Metrics:
- Component: ${COMPONENT}
- Generation: ${GENERATION}
- Improvement: +${IMPROVEMENT}%
- Timestamp: ${TIMESTAMP}
- Branch: ${BRANCH_NAME}

This is an automated evolution breakthrough commit.
COMMITEOF
)" 2>/dev/null || true

    # Create branch from current state
    git branch "${BRANCH_NAME}" 2>/dev/null || true

    # Create a tag for easy reference
    git tag -a "breakthrough-${COMPONENT}-${GENERATION}" -m "Breakthrough: ${COMPONENT} +${IMPROVEMENT}% at generation ${GENERATION}" 2>/dev/null || true

    echo "[FORK] Created branch: ${BRANCH_NAME}"
    echo "[TAG] Created tag: breakthrough-${COMPONENT}-${GENERATION}"
else
    echo "[WARN] Not in a git repository, skipping branch/tag creation"
fi

# =============================================================================
# Step 4: Update the master evolution summary
# =============================================================================

SUMMARY_FILE="$LOG_DIR/EVOLUTION_SUMMARY.md"

# Create or append to summary
if [ ! -f "$SUMMARY_FILE" ]; then
    cat > "$SUMMARY_FILE" << HEADEREOF
# AlJefra OS - Evolution Summary

## System: AlJefra OS Exokernel + NVIDIA RTX 5090 GPU Engine

This document tracks all breakthroughs achieved through GPU-accelerated self-evolution.

---

| # | Timestamp | Component | Generation | Improvement | Branch |
|---|-----------|-----------|------------|-------------|--------|
HEADEREOF
fi

# Count existing breakthroughs
BT_COUNT=$(grep -c "^|[0-9]" "$SUMMARY_FILE" 2>/dev/null || echo "0")
BT_NUM=$((BT_COUNT + 1))

# Append new breakthrough
echo "| ${BT_NUM} | ${TIMESTAMP} | ${COMPONENT} | ${GENERATION} | +${IMPROVEMENT}% | \`${BRANCH_NAME}\` |" >> "$SUMMARY_FILE"

echo "[DONE] Breakthrough #${BT_NUM} recorded successfully"
echo "  Log: ${EVOLUTION_LOG}"
echo "  Benchmark: ${BENCHMARK_FILE}"
echo "  Summary: ${SUMMARY_FILE}"
