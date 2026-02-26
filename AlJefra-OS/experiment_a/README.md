# Experiment A: AI-Directed Evolution

**Source-level optimization guided by Claude Code**

## What This Does

Claude Code reads the AlJefra OS assembly source, identifies optimization patterns, applies them, and benchmarks the results. This is "human-like" optimization — understanding what code does and making it faster.

## Prerequisites

```bash
sudo apt install nasm qemu-system-x86_64
```

Claude Code must be installed: https://docs.anthropic.com/en/docs/claude-code

## Quick Start

### Option 1: Launch Script
```bash
cd AlJefra-OS/experiment_a
./evolve_ai.sh smp
```

### Option 2: Manual Session
```bash
cd AlJefra-OS
claude
```

Then tell Claude:
```
Evolve the smp component — analyze the assembly source, identify
optimization opportunities, apply changes, build, benchmark, and
record any breakthroughs to evolution/logs/evolution_log.jsonl
```

## Available Components

| Component | Source Files | What It Does |
|-----------|-------------|--------------|
| kernel | kernel.asm, init/sys.asm | Boot, scheduler loop |
| memory | init/64.asm | Page tables, 64-bit setup |
| smp | syscalls/smp.asm | Multi-core locks, wakeup |
| network | syscalls/net.asm, drivers/net/ | Virtio network |
| storage | syscalls/nvs.asm, drivers/nvs/ | Virtio block |
| gpu_driver | drivers/gpu/nvidia.asm | RTX 5090 driver |
| bus | drivers/bus/ | PCIe discovery |
| interrupts | interrupt.asm, drivers/apic.asm | ISR handlers |
| timer | drivers/timer.asm | System timer |
| io | syscalls/io.asm, drivers/serial.asm | Serial, keyboard |
| syscalls | syscalls/system.asm | Syscall dispatch |
| scheduler | syscalls/ai_scheduler.asm | AI work scheduler |

## Workflow

1. Claude reads source assembly for the target component
2. Identifies patterns: redundant instructions, suboptimal encodings, missed optimizations
3. Applies changes directly to the .asm source
4. Builds with `./aljefra.sh build`
5. Benchmarks with `./aljefra.sh run` (QEMU)
6. If improvement >= 5%: records breakthrough to `evolution/logs/`

## Results

Results are logged to:
- `evolution/logs/evolution_log.jsonl` — shared log (both experiments)
- Git branches named `source-evo/<component>/gen<N>`

## Comparing with Experiment B

```bash
cd ../comparison
./compare.sh
```
