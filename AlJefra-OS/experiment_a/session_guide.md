# Experiment A: Session Guide

Step-by-step workflow for AI-directed kernel evolution.

## Session Setup

1. Open a terminal
2. `cd AlJefra-OS`
3. `claude` (starts Claude Code)

## Evolution Commands

### Evolve a specific component
```
Evolve the [component] component:
1. Read all .asm source files for this component
2. Identify optimization opportunities (instruction selection,
   register usage, branch prediction, alignment, lock contention)
3. Apply the most promising optimizations
4. Build with ./aljefra.sh build
5. Run benchmarks with ./aljefra.sh run
6. If improvement >= 5%, record to evolution/logs/evolution_log.jsonl
```

### Full system evolution
```
Run a full evolution cycle across all kernel components.
For each component: analyze, optimize, benchmark, record.
Start with the components most likely to yield improvements:
smp, interrupts, io, scheduler.
```

### Review previous results
```
Read evolution/logs/evolution_log.jsonl and summarize
all breakthroughs found so far, per component.
```

## Optimization Patterns Claude Should Look For

### Assembly-Level
- `cmp reg, 0` → `test reg, reg`
- `mov reg, 0` → `xor reg, reg`
- `shl reg, 1` → `add reg, reg`
- Redundant register saves (push/pop not needed)
- Branch prediction hints (likely/unlikely paths)
- Alignment of hot loop targets to 16-byte boundaries

### Architectural
- Spinlock → PAUSE instruction in busy-wait
- HLT → MWAIT for lower-latency wakeup
- Serial I/O → FIFO batch mode
- LOCK CMPXCHG → LOCK-free alternatives where possible
- Interrupt coalescing for network packets

### Data Structure
- Linear scans → bitmap operations (BSF/BSR)
- Sequential dispatch → jump table
- Small constant arrays → immediate operands

## Safety Rules

Claude MUST preserve:
- Jump table at kernel offset 0x10-0xA8
- Stack balance (push/pop pairs)
- IRETQ stack frame format
- LOCK prefix on atomic operations
- All hardware register addresses and protocols
