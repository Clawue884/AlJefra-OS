#!/bin/bash
#
# AlJefra OS — AI-Directed Evolution Launcher (Experiment A)
# Opens Claude Code in the AlJefra OS directory for source-level optimization.
#
# Usage: ./evolve_ai.sh [component]
#
set -e

COMPONENT="${1:-}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ALJEFRA_DIR="$SCRIPT_DIR/.."

echo "============================================"
echo "  AlJefra OS — AI-Directed Evolution"
echo "  Experiment A: Source-Level Optimization"
echo "============================================"
echo ""

# Check for claude
if ! command -v claude &> /dev/null; then
    echo "Error: 'claude' CLI not found."
    echo "Install Claude Code: https://docs.anthropic.com/en/docs/claude-code"
    exit 1
fi

echo "Starting Claude Code session..."
echo ""
echo "In the session, tell Claude:"
echo ""

if [ -n "$COMPONENT" ]; then
    echo "  \"Evolve the $COMPONENT component — analyze, optimize, benchmark, record\""
else
    echo "  \"Evolve [component_name] — analyze, optimize, benchmark, record\""
    echo ""
    echo "Available components:"
    echo "  kernel, memory, smp, network, storage, gpu_driver, bus,"
    echo "  interrupts, timer, io, syscalls, vram_alloc, cmd_queue,"
    echo "  dma, scheduler"
fi

echo ""
echo "Claude will:"
echo "  1. Read the source assembly for the component"
echo "  2. Identify optimization opportunities"
echo "  3. Apply source-level optimizations"
echo "  4. Build and benchmark the changes"
echo "  5. Record breakthroughs to evolution/logs/"
echo ""
echo "Press Enter to launch Claude Code..."
read -r

cd "$ALJEFRA_DIR"
claude
