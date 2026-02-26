/*
 * AlJefra OS — Binary Evolution Engine Configuration
 * All constants, component IDs, and GA parameters
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stddef.h>

/* ── Build Configuration ─────────────────────────────────────────── */
#ifdef HAVE_CUDA
#define USE_GPU_FITNESS  1
#define USE_GPU_MUTATION 1
#else
#define USE_GPU_FITNESS  0
#define USE_GPU_MUTATION 0
#endif

/* ── Kernel Layout ───────────────────────────────────────────────── */
#define KERNEL_BASE_ADDR      0x100000
#define KERNEL_MAX_SIZE       (64 * 1024)  /* 64 KB */
#define KERNEL_JUMP_TABLE_OFF 0x0010
#define KERNEL_JUMP_TABLE_END 0x00A8       /* Last entry + 8 */

/* Immutable region: jump table at 0x100010 - 0x1000A8 */
#define IMMUTABLE_START       (KERNEL_BASE_ADDR + KERNEL_JUMP_TABLE_OFF)
#define IMMUTABLE_END         (KERNEL_BASE_ADDR + KERNEL_JUMP_TABLE_END)

/* ── Component IDs ───────────────────────────────────────────────── */
typedef enum {
    COMP_KERNEL      = 0,   /* Core kernel (boot, scheduler loop) */
    COMP_MEMORY      = 1,   /* Memory management */
    COMP_SMP         = 2,   /* Multi-core / SMP */
    COMP_NETWORK     = 3,   /* Network stack */
    COMP_STORAGE     = 4,   /* NVS / storage subsystem */
    COMP_GPU_DRIVER  = 5,   /* GPU driver (nvidia.asm) */
    COMP_BUS         = 6,   /* PCIe / bus controller */
    COMP_INTERRUPTS  = 7,   /* Interrupt handling */
    COMP_TIMER       = 8,   /* Timer subsystem */
    COMP_IO          = 9,   /* I/O subsystem */
    COMP_SYSCALLS    = 10,  /* Syscall dispatch */
    COMP_VRAM_ALLOC  = 11,  /* VRAM allocator */
    COMP_CMD_QUEUE   = 12,  /* GPU command queue */
    COMP_DMA         = 13,  /* DMA engine */
    COMP_SCHEDULER   = 14,  /* AI workload scheduler */
    COMP_COUNT       = 15
} component_id_t;

/* Defined in recorder.c, declared here for all modules */
extern const char *component_names[COMP_COUNT];

/* ── Genetic Algorithm Parameters ────────────────────────────────── */
#define GA_POPULATION_SIZE     1024    /* 1K threads — fast security sweep */
#define GA_ELITE_COUNT         64
#define GA_TOURNAMENT_SIZE     8
#define GA_MUTATION_RATE       0.15    /* 15% chance per instruction */
#define GA_CROSSOVER_RATE      0.7     /* 70% crossover vs clone */
#define GA_MAX_GENERATIONS     1000
#define GA_DEFAULT_GENERATIONS 50
#define GA_STAGNATION_LIMIT    20      /* Stop if no improvement for N gens */

/* ── Mutation Types ──────────────────────────────────────────────── */
typedef enum {
    MUT_SUBSTITUTE   = 0,   /* Replace instruction with equivalent */
    MUT_NOP_ELIM     = 1,   /* Remove NOP / dead instructions */
    MUT_REORDER      = 2,   /* Swap independent instructions */
    MUT_REG_RENAME   = 3,   /* Use different register */
    MUT_ALIGNMENT    = 4,   /* Adjust alignment padding */
    MUT_FUSION       = 5,   /* Fuse adjacent instructions */
    MUT_TYPE_COUNT   = 6
} mutation_type_t;

/* ── Fitness / Benchmark ─────────────────────────────────────────── */
#define FITNESS_PREDICT_BATCH  4096    /* Candidates per batch */
#define FITNESS_TOP_N          10      /* Top predictions → QEMU test */
#define QEMU_TIMEOUT_SECS      8      /* Max time per QEMU benchmark */
#define QEMU_BOOT_GRACE_SECS   3      /* Wait for OS to boot */
#define BREAKTHROUGH_THRESHOLD 5.0     /* Minimum % improvement */

/* ── QEMU Configuration ─────────────────────────────────────────── */
#define QEMU_BINARY   "qemu-system-x86_64"
#define QEMU_MEMORY   "256"
#define QEMU_CORES    "4"
#define QEMU_MACHINE  "q35"
#define QEMU_CPU      "Westmere"
#define SERIAL_LOG    "serial.log"

/* ── File Paths (relative to experiment_b/ working dir) ──────────── */
#define KERNEL_ASM     "../aljefra/src/kernel.asm"
#define KERNEL_BIN     "../sys/kernel.sys"
#define KERNEL_LST     "../sys/kernel-debug.txt"
#define DISK_IMAGE     "../sys/aljefra_os.img"
#define BUILD_SCRIPT   "../aljefra.sh"
#define GUIDES_DIR     "guides/"
#define RESULTS_DIR    "results/"
#define EVOLUTION_LOG  "../evolution/logs/evolution_log.jsonl"

/* ── Instruction Decoder ─────────────────────────────────────────── */
#define MAX_INSTRUCTION_LEN    15     /* x86-64 max instruction length */
#define MAX_INSTRUCTIONS       8192   /* Max instructions in kernel */
#define MAX_FUNCTIONS          512    /* Max function boundaries */

/* ── Listing Parser ──────────────────────────────────────────────── */
#define MAX_LISTING_LINES      32768
#define MAX_LABEL_LEN          128
#define MAX_SOURCE_LINE_LEN    512

/* ── Population ──────────────────────────────────────────────────── */
#define MAX_GENOME_SIZE        KERNEL_MAX_SIZE  /* 64KB max */

/* ── Per-Component Guide ─────────────────────────────────────────── */
#define MAX_GUIDE_FUNCTIONS    64
#define MAX_GUIDE_INVARIANTS   32
#define MAX_GUIDE_OPPORTUNITIES 32
#define MAX_IMMUTABLE_REGIONS  16

/* ── Breakthrough Recording ──────────────────────────────────────── */
#define MAX_BREAKTHROUGHS      4096
#define BREAKTHROUGH_BRANCH_FMT "binary-evo/%s/gen%04d"

/* ── Thread / OpenMP ─────────────────────────────────────────────── */
#ifndef NUM_THREADS
#define NUM_THREADS 0  /* 0 = auto-detect */
#endif

/* ── Common Structures ───────────────────────────────────────────── */

/* Decoded instruction */
typedef struct {
    uint64_t address;              /* Virtual address */
    uint8_t  bytes[MAX_INSTRUCTION_LEN];
    uint8_t  length;               /* Instruction byte length */
    uint8_t  has_lock_prefix;      /* LOCK prefix present */
    uint8_t  is_branch;            /* JMP, Jcc, CALL, RET */
    uint8_t  is_ret;               /* RET/IRETQ */
    uint8_t  is_nop;               /* NOP/multi-byte NOP */
    uint8_t  is_push;              /* PUSH */
    uint8_t  is_pop;               /* POP */
    int32_t  branch_target;        /* Relative branch offset */
    uint16_t latency_cycles;       /* Predicted latency */
    uint16_t component_id;         /* Which component owns this */
    uint16_t source_line;          /* NASM listing source line */
    char     label[MAX_LABEL_LEN]; /* Label if this starts a function */
} instruction_t;

/* Component binary region */
typedef struct {
    component_id_t id;
    const char    *name;
    uint64_t       start_addr;      /* First byte address */
    uint64_t       end_addr;        /* Last byte + 1 */
    uint32_t       num_instructions;
    uint32_t       first_instr_idx; /* Index into instruction array */
    uint32_t       num_functions;
    char           functions[MAX_GUIDE_FUNCTIONS][MAX_LABEL_LEN];
} component_region_t;

/* Individual in the GA population */
typedef struct {
    uint8_t  *genome;              /* Binary bytes */
    uint32_t  genome_size;
    double    predicted_fitness;    /* From latency model */
    double    actual_fitness;       /* From QEMU benchmark (0 if not tested) */
    uint32_t  generation;
    uint32_t  mutations_applied;
    uint8_t   validated;           /* 1 if QEMU-tested */
    uint8_t   is_breakthrough;     /* 1 if > threshold improvement */
} individual_t;

/* Benchmark result from QEMU */
typedef struct {
    double kernel_latency_us;
    double mem_bandwidth_mbs;
    double smp_scaling_pct;
    double net_latency_us;
    double nvs_latency_us;
    double gpu_latency_us;
    double composite_score;         /* Weighted average */
    int    valid;                   /* 1 if parsed successfully */
    int    boot_success;            /* 1 if OS booted */
} benchmark_result_t;

/* Breakthrough record */
typedef struct {
    component_id_t component;
    uint32_t generation;
    double   improvement_pct;
    double   baseline_score;
    double   evolved_score;
    char     branch_name[256];
    char     timestamp[32];
    uint8_t  *binary_diff;         /* Changed bytes */
    uint32_t  diff_size;
} breakthrough_t;

#endif /* CONFIG_H */
