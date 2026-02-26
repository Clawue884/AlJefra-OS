/*
 * AlJefra OS AI — Binary Evolution Engine
 * Main orchestrator: CLI entry, full evolution loop.
 *
 * Usage: ./evolve_bin [component] [generations]
 *   component:   kernel|memory|smp|network|storage|gpu_driver|bus|
 *                interrupts|timer|io|syscalls|vram_alloc|cmd_queue|
 *                dma|scheduler|all
 *   generations:  Number of GA generations (default: 50)
 */
#include "config.h"
#include "decoder/x86_decode.h"
#include "extractor/listing_parser.h"
#include "extractor/component_map.h"
#include "mutator/mutator.h"
#include "fitness/fitness_cpu.h"
#include "validator/qemu_runner.h"
#include "ga/population.h"
#include "recorder/recorder.h"
#include "../cuda/cuda_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>

static int use_gpu = 0;  /* Set to 1 if CUDA initialized successfully */

static volatile int running = 1;

static void signal_handler(int sig) {
    (void)sig;
    printf("\nReceived interrupt — finishing current generation...\n");
    running = 0;
}

/* Parse component name from CLI argument */
static component_id_t parse_component(const char *name) {
    for (int i = 0; i < COMP_COUNT; i++) {
        if (strcmp(name, component_names[i]) == 0) {
            return (component_id_t)i;
        }
    }
    fprintf(stderr, "Unknown component: %s\n", name);
    fprintf(stderr, "Valid components: ");
    for (int i = 0; i < COMP_COUNT; i++) {
        fprintf(stderr, "%s%s", component_names[i],
                i < COMP_COUNT - 1 ? ", " : "\n");
    }
    exit(1);
}

/* Load kernel binary from file */
static uint8_t *load_kernel(const char *path, uint32_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open kernel binary: %s\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0 || fsize > KERNEL_MAX_SIZE) {
        fprintf(stderr, "Error: Invalid kernel size: %ld bytes\n", fsize);
        fclose(f);
        return NULL;
    }

    uint8_t *buf = malloc(fsize);
    if (!buf) { fclose(f); return NULL; }

    fread(buf, 1, fsize, f);
    fclose(f);

    *size = (uint32_t)fsize;
    return buf;
}

/* Run the evolution loop for a single component */
static int evolve_component(component_id_t comp, int max_generations,
                            const uint8_t *kernel_bin, uint32_t kernel_size,
                            const listing_t *listing,
                            const instruction_t *instructions,
                            int num_instructions,
                            const component_region_t *region) {
    char work_dir[512];
    snprintf(work_dir, sizeof(work_dir), "%s", ".");
    printf("\n=== Evolving: %s ===\n", component_names[comp]);
    printf("  Region: 0x%06lx - 0x%06lx (%u instructions)\n",
           (unsigned long)region->start_addr,
           (unsigned long)region->end_addr,
           region->num_instructions);

    /* Get baseline QEMU benchmark */
    benchmark_result_t baseline_bench;
    printf("  Running baseline QEMU benchmark...\n");
    if (qemu_validate(kernel_bin, kernel_size, work_dir, &baseline_bench)) {
        printf("  Baseline QEMU score: %.4f\n",
               fitness_composite_score(&baseline_bench));
    } else {
        printf("  Warning: Baseline QEMU benchmark failed (will use prediction only)\n");
        memset(&baseline_bench, 0, sizeof(baseline_bench));
    }

    int total_breakthroughs = 0;

#if USE_GPU_FITNESS
    /*
     * ── GPU Function-by-Function Evolution ────────────────────────
     * 1. Get all functions in this component with their byte ranges
     * 2. Sort by instruction count (costliest first)
     * 3. For each function: launch GPU evolve kernel targeting only
     *    that function's byte range
     * 4. Validate best candidates with QEMU
     */
    if (use_gpu) {
        /* Build function table: name, start_offset, end_offset, num_instructions */
        typedef struct {
            char name[MAX_LABEL_LEN];
            int start_off;   /* Byte offset from kernel start */
            int end_off;
            int num_instrs;
            float cost;       /* Total latency estimate */
        } func_info_t;

        func_info_t funcs[MAX_GUIDE_FUNCTIONS];
        int nfuncs = 0;

        /* Get all labels + addresses from listing */
        #define MAX_FUNC_LABELS 512
        char all_labels[MAX_FUNC_LABELS][MAX_LABEL_LEN];
        uint64_t all_addrs[MAX_FUNC_LABELS];
        int total_labels = listing_get_labels(listing, all_labels,
                                               all_addrs, MAX_FUNC_LABELS);

        /* Find functions within this component's region */
        for (int i = 0; i < total_labels && nfuncs < MAX_GUIDE_FUNCTIONS; i++) {
            if (all_addrs[i] < region->start_addr ||
                all_addrs[i] >= region->end_addr) continue;

            func_info_t *fn = &funcs[nfuncs];
            strncpy(fn->name, all_labels[i], MAX_LABEL_LEN - 1);
            fn->start_off = (int)(all_addrs[i] - KERNEL_BASE_ADDR);
            fn->num_instrs = 0;
            fn->cost = 0;

            /* Find end: next label's address or region end */
            uint64_t end_addr = region->end_addr;
            for (int j = i + 1; j < total_labels; j++) {
                if (all_addrs[j] > all_addrs[i]) {
                    end_addr = all_addrs[j];
                    break;
                }
            }
            fn->end_off = (int)(end_addr - KERNEL_BASE_ADDR);

            /* Count instructions in this function */
            for (int j = 0; j < num_instructions; j++) {
                if (instructions[j].address >= all_addrs[i] &&
                    instructions[j].address < end_addr) {
                    fn->num_instrs++;
                    fn->cost += instructions[j].latency_cycles;
                }
            }

            if (fn->end_off > fn->start_off && fn->num_instrs > 0) {
                nfuncs++;
            }
        }

        /* Sort by instruction count (costliest first) */
        for (int i = 0; i < nfuncs - 1; i++) {
            for (int j = i + 1; j < nfuncs; j++) {
                if (funcs[j].num_instrs > funcs[i].num_instrs) {
                    func_info_t tmp = funcs[i];
                    funcs[i] = funcs[j];
                    funcs[j] = tmp;
                }
            }
        }

        printf("\n  Functions sorted by cost (top %d):\n",
               nfuncs > 20 ? 20 : nfuncs);
        for (int i = 0; i < nfuncs && i < 20; i++) {
            printf("    %2d. %-30s %4d instrs  [0x%04x-0x%04x] (%d bytes)\n",
                   i + 1, funcs[i].name, funcs[i].num_instrs,
                   funcs[i].start_off, funcs[i].end_off,
                   funcs[i].end_off - funcs[i].start_off);
        }

        uint8_t *best_genome = malloc(kernel_size);
        uint8_t *evolved_kernel = malloc(kernel_size);
        if (!best_genome || !evolved_kernel) return -1;
        memcpy(evolved_kernel, kernel_bin, kernel_size);

        float baseline_fitness = 0;
        /* Get baseline from GPU */
        {
            float dummy;
            cuda_gpu_evolve(kernel_bin, kernel_size, best_genome, &dummy,
                            1, 0, 0, 0, 1);
            baseline_fitness = dummy;
        }
        printf("\n  Baseline GPU fitness: %.2f\n", baseline_fitness);

        int funcs_improved = 0;
        int gpu_pop = GA_POPULATION_SIZE;

        /* ── Ratio Allocation ──
         * Scale iterations proportional to function size:
         *   tiny  (< 8 instrs):   50K iters/thread
         *   small (8-15 instrs): 150K
         *   medium(16-30):       350K
         *   large (31-50):       500K
         *   huge  (51+):         750K
         * This focuses GPU time where it matters most. */
        #define ITERS_TINY    50000
        #define ITERS_SMALL  150000
        #define ITERS_MEDIUM 350000
        #define ITERS_LARGE  500000
        #define ITERS_HUGE   750000

        /* ── Security Analysis Storage ── */
        typedef struct {
            char name[MAX_LABEL_LEN];
            int  num_instrs;
            int  byte_size;
            float fragility;         /* % fitness drop from worst mutation */
            float best_fitness;
            float worst_fitness;
            float avg_mutations;     /* avg mutations that actually applied */
            int   improved;          /* did optimization find an improvement? */
            float improvement_pct;
            int   is_dead_code;      /* 0 mutations applied = likely dead/data */
        } func_analysis_t;

        func_analysis_t analysis[MAX_GUIDE_FUNCTIONS];
        int n_analyzed = 0;

        /* Compute total instructions for time estimate */
        int total_instrs = 0;
        for (int i = 0; i < nfuncs; i++) total_instrs += funcs[i].num_instrs;

        printf("\n  ── GPU Evolution + Security Analysis ──\n");
        printf("  Ratio allocation: scaling GPU time by function size\n");
        printf("  Total: %d functions, %d instructions\n\n", nfuncs, total_instrs);

        for (int fi = 0; fi < nfuncs && fi < max_generations && running; fi++) {
            func_info_t *fn = &funcs[fi];

            if (fn->num_instrs < 3) {
                printf("  [%d/%d] %-30s SKIP (too small)\n",
                       fi + 1, nfuncs, fn->name);
                continue;
            }

            /* Ratio allocation: pick iterations based on function size */
            int iters_per_thread;
            if (fn->num_instrs >= 51)      iters_per_thread = ITERS_HUGE;
            else if (fn->num_instrs >= 31) iters_per_thread = ITERS_LARGE;
            else if (fn->num_instrs >= 16) iters_per_thread = ITERS_MEDIUM;
            else if (fn->num_instrs >= 8)  iters_per_thread = ITERS_SMALL;
            else                           iters_per_thread = ITERS_TINY;

            printf("  [%d/%d] %-30s %d instrs [0x%04x-0x%04x] → %dK iters\n",
                   fi + 1, nfuncs, fn->name, fn->num_instrs,
                   fn->start_off, fn->end_off, iters_per_thread / 1000);

            /* Run GPU evolution with security tracking */
            float gpu_best_fitness = 0;
            struct gpu_security_stats sec = {0};

            if (cuda_gpu_evolve_full(evolved_kernel, kernel_size,
                                      best_genome, &gpu_best_fitness,
                                      gpu_pop, iters_per_thread,
                                      (unsigned)rand(),
                                      fn->start_off, fn->end_off,
                                      &sec) != 0) {
                printf("    GPU kernel failed\n");
                continue;
            }

            /* Record analysis */
            func_analysis_t *a = &analysis[n_analyzed];
            strncpy(a->name, fn->name, MAX_LABEL_LEN - 1);
            a->num_instrs = fn->num_instrs;
            a->byte_size = fn->end_off - fn->start_off;
            a->fragility = sec.fragility;
            a->best_fitness = sec.best_fitness;
            a->worst_fitness = sec.worst_fitness;
            a->avg_mutations = sec.avg_mutations;
            a->is_dead_code = (sec.avg_mutations < 0.01f) ? 1 : 0;
            a->improved = 0;
            a->improvement_pct = 0;
            n_analyzed++;

            float improvement = gpu_best_fitness - baseline_fitness;
            float pct = (baseline_fitness > 0) ?
                        (improvement / baseline_fitness * 100.0f) : 0;

            /* Print security info */
            printf("    Security: fragility=%.1f%% worst=%.2f avg_muts=%.0f",
                   sec.fragility, sec.worst_fitness, sec.avg_mutations);
            if (a->is_dead_code)
                printf(" [DEAD CODE?]");
            if (sec.fragility > 5.0f)
                printf(" [FRAGILE!]");
            if (sec.fragility > 15.0f)
                printf(" [CRITICAL VULNERABILITY]");
            printf("\n");

            if (improvement > 0.01f) {
                printf("    Optimize: %.2f → %.2f (+%.2f, +%.2f%%)\n",
                       baseline_fitness, gpu_best_fitness, improvement, pct);

                /* Validate safety */
                if (mutator_validate(evolved_kernel, best_genome, kernel_size,
                                     instructions, num_instructions)) {
                    /* Accept the improvement — update evolved kernel */
                    memcpy(evolved_kernel, best_genome, kernel_size);
                    baseline_fitness = gpu_best_fitness;
                    funcs_improved++;
                    a->improved = 1;
                    a->improvement_pct = pct;
                    printf("    ACCEPTED ✓ (cumulative fitness: %.2f)\n",
                           baseline_fitness);

                    if (pct >= BREAKTHROUGH_THRESHOLD) {
                        total_breakthroughs++;
                        printf("    *** BREAKTHROUGH in %s: +%.1f%% ***\n",
                               fn->name, pct);
                        breakthrough_t bt = {0};
                        bt.component = comp;
                        bt.generation = fi;
                        bt.improvement_pct = pct;
                        bt.baseline_score = baseline_fitness - improvement;
                        bt.evolved_score = baseline_fitness;
                        recorder_timestamp(bt.timestamp, sizeof(bt.timestamp));
                        recorder_save_breakthrough(&bt, evolved_kernel,
                                                  kernel_size, work_dir);
                    }
                } else {
                    printf("    REJECTED (safety check failed)\n");
                }
            } else {
                printf("    no improvement (%.2f)\n", gpu_best_fitness);
            }
        }

        /* ── Security Report ── */
        printf("\n  ╔══════════════════════════════════════════════════════════════╗\n");
        printf("  ║              SECURITY & VULNERABILITY REPORT                ║\n");
        printf("  ╚══════════════════════════════════════════════════════════════╝\n\n");

        /* Sort by fragility (most fragile first) for security report */
        for (int i = 0; i < n_analyzed - 1; i++) {
            for (int j = i + 1; j < n_analyzed; j++) {
                if (analysis[j].fragility > analysis[i].fragility) {
                    func_analysis_t tmp = analysis[i];
                    analysis[i] = analysis[j];
                    analysis[j] = tmp;
                }
            }
        }

        /* Critical vulnerabilities */
        int n_critical = 0, n_fragile = 0, n_dead = 0, n_robust = 0;
        for (int i = 0; i < n_analyzed; i++) {
            if (analysis[i].fragility > 15.0f) n_critical++;
            else if (analysis[i].fragility > 5.0f) n_fragile++;
            if (analysis[i].is_dead_code) n_dead++;
            if (analysis[i].fragility < 1.0f) n_robust++;
        }

        printf("  Summary: %d analyzed | %d critical | %d fragile | %d robust | %d dead code\n\n",
               n_analyzed, n_critical, n_fragile, n_robust, n_dead);

        if (n_critical > 0) {
            printf("  🔴 CRITICAL (>15%% fitness drop from single mutation):\n");
            for (int i = 0; i < n_analyzed; i++) {
                if (analysis[i].fragility > 15.0f) {
                    printf("     %-30s fragility=%5.1f%% (%d instrs, %d bytes)\n",
                           analysis[i].name, analysis[i].fragility,
                           analysis[i].num_instrs, analysis[i].byte_size);
                }
            }
            printf("\n");
        }

        if (n_fragile > 0) {
            printf("  🟡 FRAGILE (5-15%% fitness drop):\n");
            for (int i = 0; i < n_analyzed; i++) {
                if (analysis[i].fragility > 5.0f && analysis[i].fragility <= 15.0f) {
                    printf("     %-30s fragility=%5.1f%% (%d instrs, %d bytes)\n",
                           analysis[i].name, analysis[i].fragility,
                           analysis[i].num_instrs, analysis[i].byte_size);
                }
            }
            printf("\n");
        }

        if (n_dead > 0) {
            printf("  ⚪ DEAD CODE (no mutations applied — possibly data or unreachable):\n");
            for (int i = 0; i < n_analyzed; i++) {
                if (analysis[i].is_dead_code) {
                    printf("     %-30s (%d instrs, %d bytes)\n",
                           analysis[i].name, analysis[i].num_instrs,
                           analysis[i].byte_size);
                }
            }
            printf("\n");
        }

        printf("  🟢 MOST ROBUST (< 1%% fitness drop):\n");
        for (int i = n_analyzed - 1; i >= 0 && i >= n_analyzed - 10; i--) {
            if (analysis[i].fragility < 1.0f && !analysis[i].is_dead_code) {
                printf("     %-30s fragility=%5.2f%% (%d instrs)\n",
                       analysis[i].name, analysis[i].fragility,
                       analysis[i].num_instrs);
            }
        }

        printf("\n  ── Optimization Results ──\n");
        for (int i = 0; i < n_analyzed; i++) {
            if (analysis[i].improved) {
                printf("    ✓ %-30s +%.2f%%\n",
                       analysis[i].name, analysis[i].improvement_pct);
            }
        }

        /* Final QEMU validation of accumulated improvements */
        if (funcs_improved > 0) {
            printf("\n  ── Final QEMU Validation ──\n");
            printf("  %d functions improved, testing evolved kernel...\n",
                   funcs_improved);
            benchmark_result_t bench;
            if (qemu_validate(evolved_kernel, kernel_size, work_dir, &bench)) {
                double score = fitness_composite_score(&bench);
                printf("  QEMU score: %.4f\n", score);
            } else {
                printf("  QEMU: kernel booted successfully\n");
            }
        }

        printf("\n=== GPU Evolution Complete: %s ===\n", component_names[comp]);
        printf("  Functions analyzed: %d\n", nfuncs);
        printf("  Functions improved: %d\n", funcs_improved);
        printf("  Final GPU fitness: %.2f\n", baseline_fitness);
        printf("  Breakthroughs: %d\n", total_breakthroughs);

        free(best_genome);
        free(evolved_kernel);
        return total_breakthroughs;
    }
#endif

    /* ── CPU Evolution Path (fallback) ────────────────────────────── */
    population_t pop;
    if (population_init(&pop, 256, kernel_bin, kernel_size) != 0) {
        fprintf(stderr, "Error: Failed to initialize population\n");
        return -1;
    }

    pop.baseline_fitness = fitness_predict(kernel_bin, kernel_size, KERNEL_BASE_ADDR);
    printf("  Baseline predicted fitness: %.2f\n", pop.baseline_fitness);

    int stagnation = 0;
    double prev_best = 0.0;

    for (int gen = 0; gen < max_generations && running; gen++) {
        pop.generation = gen;

        /* Phase 1: Mutate */
        for (int i = GA_ELITE_COUNT; i < pop.size; i++) {
            memcpy(pop.individuals[i].genome, kernel_bin, kernel_size);
            pop.individuals[i].mutations_applied = mutator_apply_n(
                pop.individuals[i].genome, kernel_size,
                instructions, num_instructions, region, 1 + (rand() % 3));
            pop.individuals[i].validated = 0;
            pop.individuals[i].is_breakthrough = 0;
        }

        /* Phase 2: Predict fitness */
        for (int i = 0; i < pop.size; i++) {
            pop.individuals[i].predicted_fitness =
                fitness_predict(pop.individuals[i].genome, kernel_size,
                                KERNEL_BASE_ADDR);
        }

        /* Phase 3: Sort by predicted fitness */
        population_sort(&pop);
        population_print_stats(&pop);

        /* Phase 4: Validate top N candidates with QEMU */
        int top_indices[FITNESS_TOP_N];
        population_top_n(&pop, FITNESS_TOP_N, top_indices);

        for (int t = 0; t < FITNESS_TOP_N && t < pop.size; t++) {
            int idx = top_indices[t];
            individual_t *ind = &pop.individuals[idx];

            /* Skip if predicted fitness isn't better than baseline */
            if (ind->predicted_fitness <= pop.baseline_fitness) continue;

            /* Validate safety invariants first */
            if (!mutator_validate(kernel_bin, ind->genome, kernel_size,
                                  instructions, num_instructions)) {
                ind->predicted_fitness = 0;  /* Kill unsafe individuals */
                continue;
            }

            /* QEMU benchmark */
            benchmark_result_t bench;
            if (qemu_validate(ind->genome, kernel_size, work_dir, &bench)) {
                ind->actual_fitness = fitness_composite_score(&bench);
                ind->validated = 1;

                double improvement = fitness_improvement_pct(
                    &baseline_bench, &bench);

                if (improvement >= BREAKTHROUGH_THRESHOLD) {
                    /* BREAKTHROUGH! */
                    ind->is_breakthrough = 1;
                    total_breakthroughs++;

                    breakthrough_t bt = {0};
                    bt.component = comp;
                    bt.generation = gen;
                    bt.improvement_pct = improvement;
                    bt.baseline_score = fitness_composite_score(&baseline_bench);
                    bt.evolved_score = ind->actual_fitness;
                    recorder_timestamp(bt.timestamp, sizeof(bt.timestamp));

                    recorder_save_breakthrough(&bt, ind->genome,
                                              kernel_size, work_dir);
                }
            }
        }

        /* Phase 5: Create next generation */
        /* Elite individuals (top GA_ELITE_COUNT) survive unchanged */
        /* Rest are replaced by offspring of tournament selection + crossover */
        int num_offspring = pop.size - GA_ELITE_COUNT;
        individual_t *offspring = calloc(num_offspring, sizeof(individual_t));
        if (offspring) {
            for (int i = 0; i < num_offspring; i++) {
                double r = (double)rand() / RAND_MAX;
                if (r < GA_CROSSOVER_RATE) {
                    /* Crossover */
                    int p1 = tournament_select(&pop, GA_TOURNAMENT_SIZE);
                    int p2 = tournament_select(&pop, GA_TOURNAMENT_SIZE);
                    crossover(&pop.individuals[p1], &pop.individuals[p2],
                              &offspring[i], instructions, num_instructions);
                } else {
                    /* Clone with mutation */
                    int p = tournament_select(&pop, GA_TOURNAMENT_SIZE);
                    offspring[i].genome = malloc(kernel_size);
                    if (offspring[i].genome) {
                        memcpy(offspring[i].genome,
                               pop.individuals[p].genome, kernel_size);
                        offspring[i].genome_size = kernel_size;
                        offspring[i].generation = gen + 1;
                    }
                }
            }
            population_replace_worst(&pop, offspring, num_offspring);
            free(offspring);
        }

        /* Phase 6: Log generation results */
        double avg = 0;
        for (int i = 0; i < pop.size; i++)
            avg += pop.individuals[i].predicted_fitness;
        avg /= pop.size;

        recorder_log_generation(gen, comp, pop.best_fitness, avg,
                               pop.size, total_breakthroughs, work_dir);

        /* Check for stagnation */
        if (pop.best_fitness <= prev_best * 1.001) {
            stagnation++;
            if (stagnation >= GA_STAGNATION_LIMIT) {
                printf("  Stagnation detected after %d generations. Stopping.\n",
                       stagnation);
                break;
            }
        } else {
            stagnation = 0;
        }
        prev_best = pop.best_fitness;
    }

    printf("\n=== Evolution Complete: %s ===\n", component_names[comp]);
    printf("  Generations: %u\n", pop.generation);
    printf("  Breakthroughs: %d\n", total_breakthroughs);
    printf("  Best predicted fitness: %.2f (baseline: %.2f)\n",
           pop.best_fitness, pop.baseline_fitness);

    population_free(&pop);
    return total_breakthroughs;
}

int main(int argc, char *argv[]) {
    printf("AlJefra OS AI — Binary Evolution Engine\n");
    printf("========================================\n\n");

    /* Parse arguments */
    const char *comp_name = "interrupts";  /* Default component */
    int max_gen = GA_DEFAULT_GENERATIONS;

    if (argc >= 2) comp_name = argv[1];
    if (argc >= 3) max_gen = atoi(argv[2]);
    if (max_gen <= 0) max_gen = GA_DEFAULT_GENERATIONS;

    component_id_t target_comp = parse_component(comp_name);

    /* Setup */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    srand((unsigned)time(NULL));
    mutator_init((unsigned)time(NULL));

    /* Check prerequisites */
    printf("Checking prerequisites...\n");
    if (qemu_init("src") != 0) {
        return 1;
    }
    printf("  OK\n\n");

    /* Initialize GPU (if available) */
    printf("Initializing GPU...\n");
#if USE_GPU_FITNESS || USE_GPU_MUTATION
    if (cuda_init() == 0) {
        use_gpu = 1;
        printf("  GPU acceleration: ENABLED (%s)\n\n", cuda_get_device_name());
    } else {
        printf("  GPU acceleration: FAILED (falling back to CPU)\n\n");
    }
#else
    printf("  GPU acceleration: DISABLED (build without CUDA)\n\n");
#endif

    /* Load kernel binary */
    printf("Loading kernel binary...\n");
    uint32_t kernel_size;
    uint8_t *kernel_bin = load_kernel(KERNEL_BIN, &kernel_size);
    if (!kernel_bin) {
        /* Try building first */
        printf("  Kernel not found, attempting build...\n");
        if (system("cd .. && bash aljefra.sh build 2>&1") != 0) {
            fprintf(stderr, "Error: Cannot build kernel\n");
            return 1;
        }
        kernel_bin = load_kernel(KERNEL_BIN, &kernel_size);
        if (!kernel_bin) return 1;
    }
    printf("  Loaded %u bytes\n\n", kernel_size);

    /* Parse listing file (if available) */
    printf("Parsing listing file...\n");
    listing_t listing;
    memset(&listing, 0, sizeof(listing));

    /* Use existing listing file (generated by build) */
    if (listing_parse(KERNEL_LST, &listing) != 0) {
        printf("  Warning: No listing file. Function boundaries will be approximated.\n");
    } else {
        printf("  Parsed %d listing lines\n", listing.num_lines);
    }

    /* Decode kernel binary */
    printf("Decoding kernel instructions...\n");
    instruction_t *instructions = malloc(MAX_INSTRUCTIONS * sizeof(instruction_t));
    if (!instructions) {
        fprintf(stderr, "Error: Out of memory\n");
        free(kernel_bin);
        listing_free(&listing);
        return 1;
    }

    int num_instrs = x86_decode_all(kernel_bin, kernel_size, KERNEL_BASE_ADDR,
                                     instructions, MAX_INSTRUCTIONS);
    printf("  Decoded %d instructions\n\n", num_instrs);

    /* Build component map */
    printf("Building component map...\n");
    component_region_t regions[COMP_COUNT];
    int num_comps = component_map_build(&listing, instructions, num_instrs,
                                         regions, COMP_COUNT);
    printf("  Found %d active components\n\n", num_comps);

    /* Create results directory */
    system("mkdir -p results");
    system("mkdir -p ../evolution/logs");

    /* Evolve target component */
    int breakthroughs;
    if (target_comp < COMP_COUNT) {
        if (regions[target_comp].num_instructions == 0) {
            printf("Warning: Component '%s' has no instructions.\n"
                   "The listing file may be missing or incomplete.\n"
                   "Falling back to full-kernel evolution.\n\n",
                   component_names[target_comp]);
            /* Fall back to evolving the full kernel */
            regions[target_comp].start_addr = KERNEL_BASE_ADDR;
            regions[target_comp].end_addr = KERNEL_BASE_ADDR + kernel_size;
            regions[target_comp].num_instructions = num_instrs;
        }
        breakthroughs = evolve_component(target_comp, max_gen,
                                          kernel_bin, kernel_size,
                                          &listing, instructions, num_instrs,
                                          &regions[target_comp]);
    } else {
        fprintf(stderr, "Invalid component\n");
        breakthroughs = 0;
    }

    /* Cleanup */
    free(instructions);
    free(kernel_bin);
    listing_free(&listing);
    qemu_cleanup("src");
    if (use_gpu) cuda_cleanup();

    printf("\n========================================\n");
    printf("Total breakthroughs: %d\n", breakthroughs);
    printf("Results saved to: experiment_b/results/\n");

    return breakthroughs > 0 ? 0 : 1;
}
