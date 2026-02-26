#!/bin/bash
#
# AlJefra OS — Binary Evolution Launcher
# One-command launch: installs deps, builds, runs.
#
# Usage: ./evolve_binary.sh [component] [generations]
#   component:   kernel|memory|smp|network|storage|gpu_driver|bus|
#                interrupts|timer|io|syscalls|vram_alloc|cmd_queue|
#                dma|scheduler
#   generations:  Number of GA generations (default: 50)
#
set -e

COMPONENT="${1:-interrupts}"
GENERATIONS="${2:-50}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "============================================"
echo "  AlJefra OS — Binary Evolution Engine"
echo "============================================"
echo ""
echo "  Component:   $COMPONENT"
echo "  Generations: $GENERATIONS"
echo ""

# ── Check / Install Dependencies ──────────────────────────────────

check_dep() {
    if ! command -v "$1" &> /dev/null; then
        echo "  [MISSING] $1"
        return 1
    else
        echo "  [OK]      $1"
        return 0
    fi
}

echo "Checking dependencies..."
NEED_INSTALL=0

check_dep nasm    || NEED_INSTALL=1
check_dep qemu-system-x86_64 || NEED_INSTALL=1
check_dep gcc     || NEED_INSTALL=1
check_dep make    || NEED_INSTALL=1

if [ "$NEED_INSTALL" -eq 1 ]; then
    echo ""
    echo "Installing missing dependencies..."
    if command -v apt &> /dev/null; then
        sudo apt update -qq
        sudo apt install -y -qq nasm qemu-system-x86 gcc make
    elif command -v dnf &> /dev/null; then
        sudo dnf install -y nasm qemu-system-x86 gcc make
    elif command -v pacman &> /dev/null; then
        sudo pacman -S --noconfirm nasm qemu-system-x86 gcc make
    else
        echo "Error: Cannot auto-install dependencies."
        echo "Please install: nasm qemu-system-x86_64 gcc make"
        exit 1
    fi
fi

# Check for CUDA (optional)
if command -v nvcc &> /dev/null; then
    echo "  [OK]      nvcc (CUDA — GPU acceleration enabled)"
else
    echo "  [INFO]    nvcc not found — using CPU-only mode"
    echo "            (Install nvidia-cuda-toolkit for GPU acceleration)"
fi

echo ""

# ── Build Kernel (if needed) ──────────────────────────────────────

ALJEFRA_DIR="$SCRIPT_DIR/.."
KERNEL_BIN="$ALJEFRA_DIR/sys/kernel.bin"

if [ ! -f "$KERNEL_BIN" ]; then
    echo "Building AlJefra OS kernel..."
    cd "$ALJEFRA_DIR"
    if [ -f aljefra.sh ]; then
        ./aljefra.sh setup 2>&1 | tail -5
        ./aljefra.sh build 2>&1 | tail -5
    else
        echo "Error: aljefra.sh not found in $ALJEFRA_DIR"
        exit 1
    fi
    cd "$SCRIPT_DIR"
    echo ""
fi

# ── Build Evolution Engine ────────────────────────────────────────

echo "Building evolution engine..."
cd "$SCRIPT_DIR"
make -j"$(nproc)" 2>&1

if [ ! -f evolve_bin ]; then
    echo "Error: Build failed"
    exit 1
fi

echo ""

# ── Create Results Directory ──────────────────────────────────────

mkdir -p results
mkdir -p "$ALJEFRA_DIR/evolution/logs"

# ── Launch Evolution ──────────────────────────────────────────────

echo "Starting binary evolution..."
echo "  Press Ctrl+C to stop gracefully"
echo ""

./evolve_bin "$COMPONENT" "$GENERATIONS"

EXIT_CODE=$?

echo ""
echo "============================================"
if [ $EXIT_CODE -eq 0 ]; then
    echo "  Evolution completed with breakthroughs!"
    echo "  Check results/ directory for details."
else
    echo "  Evolution completed (no breakthroughs)."
    echo "  Try more generations or a different component."
fi
echo "============================================"

# Show results summary
if [ -f results/breakthroughs.jsonl ]; then
    echo ""
    echo "Breakthroughs found:"
    cat results/breakthroughs.jsonl | while read -r line; do
        comp=$(echo "$line" | grep -o '"component":"[^"]*"' | cut -d'"' -f4)
        improv=$(echo "$line" | grep -o '"improvement_pct":[0-9.]*' | cut -d: -f2)
        gen=$(echo "$line" | grep -o '"generation":[0-9]*' | cut -d: -f2)
        echo "  - $comp: +${improv}% (generation $gen)"
    done
fi

exit $EXIT_CODE
