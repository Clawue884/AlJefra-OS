/*
 * Component Map
 * Maps AlJefra OS source files to the 15 evolution components
 */
#include "component_map.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Source file → component ID mapping */
typedef struct {
    const char     *pattern;  /* Substring match in filename */
    component_id_t  comp;
} file_map_entry_t;

static const file_map_entry_t file_map[] = {
    /* Drivers */
    {"drivers/gpu/nvidia",   COMP_GPU_DRIVER},
    {"drivers/bus/pcie",     COMP_BUS},
    {"drivers/bus/pci",      COMP_BUS},
    {"drivers/net/",         COMP_NETWORK},
    {"drivers/nvs/",         COMP_STORAGE},
    {"drivers/apic",         COMP_INTERRUPTS},
    {"drivers/ioapic",       COMP_INTERRUPTS},
    {"drivers/msi",          COMP_INTERRUPTS},
    {"drivers/timer",        COMP_TIMER},
    {"drivers/serial",       COMP_IO},
    {"drivers/ps2",          COMP_IO},
    {"drivers/vga",          COMP_IO},
    {"drivers/lfb/",         COMP_IO},
    {"drivers/virtio",       COMP_BUS},

    /* Syscalls */
    {"syscalls/smp",         COMP_SMP},
    {"syscalls/net",         COMP_NETWORK},
    {"syscalls/nvs",         COMP_STORAGE},
    {"syscalls/io",          COMP_IO},
    {"syscalls/bus",         COMP_BUS},
    {"syscalls/system",      COMP_SYSCALLS},
    {"syscalls/debug",       COMP_SYSCALLS},
    {"syscalls/gpu",         COMP_GPU_DRIVER},
    {"syscalls/evolve",      COMP_SYSCALLS},
    {"syscalls/ai_scheduler", COMP_SCHEDULER},

    /* Init */
    {"init/64",              COMP_MEMORY},
    {"init/bus",             COMP_BUS},
    {"init/gpu",             COMP_GPU_DRIVER},
    {"init/nvs",             COMP_STORAGE},
    {"init/net",             COMP_NETWORK},
    {"init/hid",             COMP_IO},
    {"init/sys",             COMP_KERNEL},

    /* Core */
    {"interrupt",            COMP_INTERRUPTS},
    {"kernel",               COMP_KERNEL},
    {"sysvar",               COMP_KERNEL},

    {NULL, COMP_KERNEL}  /* Default */
};

/* Label → component mapping for labels not covered by files */
typedef struct {
    const char     *prefix;
    component_id_t  comp;
} label_map_entry_t;

static const label_map_entry_t label_map[] = {
    {"b_smp_",           COMP_SMP},
    {"b_net_",           COMP_NETWORK},
    {"b_nvs_",           COMP_STORAGE},
    {"b_gpu_",           COMP_GPU_DRIVER},
    {"b_input",          COMP_IO},
    {"b_output",         COMP_IO},
    {"b_system",         COMP_SYSCALLS},
    {"b_delay",          COMP_TIMER},
    {"b_tsc",            COMP_TIMER},
    {"b_ai_sched_",      COMP_SCHEDULER},
    {"b_evolve_",        COMP_SYSCALLS},
    {"gpu_init",         COMP_GPU_DRIVER},
    {"gpu_",             COMP_GPU_DRIVER},
    {"nvidia_",          COMP_GPU_DRIVER},
    {"vram_alloc",       COMP_VRAM_ALLOC},
    {"vram_free",        COMP_VRAM_ALLOC},
    {"vram_bitmap",      COMP_VRAM_ALLOC},
    {"cmdq_",            COMP_CMD_QUEUE},
    {"dma_",             COMP_DMA},
    {"init_bus",         COMP_BUS},
    {"init_net",         COMP_NETWORK},
    {"init_nvs",         COMP_STORAGE},
    {"init_hid",         COMP_IO},
    {"init_sys",         COMP_KERNEL},
    {"init_64",          COMP_MEMORY},
    {"isr_",             COMP_INTERRUPTS},
    {"interrupt_",       COMP_INTERRUPTS},
    {"exception_",       COMP_INTERRUPTS},
    {"ap_check",         COMP_SMP},
    {"ap_clear",         COMP_SMP},
    {"ap_halt",          COMP_SMP},
    {"ap_process",       COMP_SMP},
    {"bsp",              COMP_KERNEL},
    {"start",            COMP_KERNEL},
    {NULL, COMP_KERNEL}
};

component_id_t component_from_filename(const char *filename) {
    if (!filename || !filename[0]) return COMP_KERNEL;

    for (int i = 0; file_map[i].pattern != NULL; i++) {
        if (strstr(filename, file_map[i].pattern)) {
            return file_map[i].comp;
        }
    }
    return COMP_KERNEL;
}

component_id_t component_from_label(const char *label) {
    if (!label || !label[0]) return COMP_KERNEL;

    for (int i = 0; label_map[i].prefix != NULL; i++) {
        if (strncmp(label, label_map[i].prefix,
                    strlen(label_map[i].prefix)) == 0) {
            return label_map[i].comp;
        }
    }
    return COMP_KERNEL;
}

int component_map_build(const listing_t *lst,
                        const instruction_t *instructions, int num_instructions,
                        component_region_t *regions, int max_regions) {
    /* Initialize all component regions */
    for (int i = 0; i < max_regions && i < COMP_COUNT; i++) {
        regions[i].id = (component_id_t)i;
        regions[i].name = component_names[i];
        regions[i].start_addr = UINT64_MAX;
        regions[i].end_addr = 0;
        regions[i].num_instructions = 0;
        regions[i].first_instr_idx = 0;
        regions[i].num_functions = 0;
    }

    /* Step 1: Build a sorted label→address→component map from listing */
    #define MAX_LABELS 512
    char labels[MAX_LABELS][MAX_LABEL_LEN];
    uint64_t label_addrs[MAX_LABELS];
    component_id_t label_comps[MAX_LABELS];
    int nlabels = listing_get_labels(lst, labels, label_addrs, MAX_LABELS);

    printf("  Found %d labels in listing\n", nlabels);

    for (int i = 0; i < nlabels; i++) {
        label_comps[i] = component_from_label(labels[i]);
    }

    /* Also check filename context from listing lines */
    for (int i = 0; i < lst->num_lines; i++) {
        if (lst->lines[i].is_label && lst->lines[i].label[0]) {
            /* Find matching label and apply filename-based component */
            if (lst->lines[i].filename[0]) {
                component_id_t fc = component_from_filename(lst->lines[i].filename);
                for (int j = 0; j < nlabels; j++) {
                    if (strcmp(labels[j], lst->lines[i].label) == 0) {
                        /* Filename overrides default if more specific */
                        if (fc != COMP_KERNEL) {
                            label_comps[j] = fc;
                        }
                        break;
                    }
                }
            }
        }
    }

    /* Record functions in their component regions */
    for (int i = 0; i < nlabels; i++) {
        int ci = (int)label_comps[i];
        if (ci < max_regions) {
            int fn = regions[ci].num_functions;
            if (fn < MAX_GUIDE_FUNCTIONS) {
                strncpy(regions[ci].functions[fn], labels[i], MAX_LABEL_LEN - 1);
                regions[ci].num_functions++;
            }
        }
    }

    /* Step 2: Assign each instruction to a component based on nearest label */
    for (int i = 0; i < num_instructions; i++) {
        const instruction_t *instr = &instructions[i];

        /* Find the latest label that is at or before this instruction's address */
        component_id_t comp = COMP_KERNEL;
        for (int j = nlabels - 1; j >= 0; j--) {
            if (label_addrs[j] <= instr->address) {
                comp = label_comps[j];
                break;
            }
        }

        /* Update region bounds */
        int ci = (int)comp;
        if (ci < max_regions) {
            if (instr->address < regions[ci].start_addr) {
                regions[ci].start_addr = instr->address;
                regions[ci].first_instr_idx = i;
            }
            if (instr->address + instr->length > regions[ci].end_addr) {
                regions[ci].end_addr = instr->address + instr->length;
            }
            regions[ci].num_instructions++;
        }
    }

    /* Count active regions */
    int active = 0;
    for (int i = 0; i < max_regions && i < COMP_COUNT; i++) {
        if (regions[i].num_instructions > 0) {
            active++;
            printf("  Component %-12s: 0x%06lx - 0x%06lx (%u instructions, %u functions)\n",
                   regions[i].name,
                   (unsigned long)regions[i].start_addr,
                   (unsigned long)regions[i].end_addr,
                   regions[i].num_instructions,
                   regions[i].num_functions);
        }
    }

    return active;
}

uint8_t *component_extract_binary(const uint8_t *kernel_bin,
                                  size_t kernel_size,
                                  const component_region_t *region,
                                  size_t *out_size) {
    if (region->start_addr < KERNEL_BASE_ADDR ||
        region->end_addr <= region->start_addr) {
        *out_size = 0;
        return NULL;
    }

    uint64_t offset = region->start_addr - KERNEL_BASE_ADDR;
    uint64_t length = region->end_addr - region->start_addr;

    if (offset + length > kernel_size) {
        *out_size = 0;
        return NULL;
    }

    uint8_t *buf = malloc(length);
    if (!buf) {
        *out_size = 0;
        return NULL;
    }

    memcpy(buf, kernel_bin + offset, length);
    *out_size = (size_t)length;
    return buf;
}
