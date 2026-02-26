/* SPDX-License-Identifier: MIT */
/* AlJefra OS — Flattened Device Tree (FDT) Parser
 * Parses Device Tree Blob (DTB) to discover hardware on ARM/RISC-V.
 * Architecture-independent; pure C data parsing.
 */

#ifndef ALJEFRA_DRV_DT_PARSER_H
#define ALJEFRA_DRV_DT_PARSER_H

#include "../../hal/hal.h"

/* ── FDT Header (40 bytes, big-endian) ── */
#define FDT_MAGIC       0xD00DFEED
#define FDT_VERSION     17          /* Minimum supported version */

typedef struct __attribute__((packed)) {
    uint32_t magic;             /* 0xD00DFEED (big-endian) */
    uint32_t totalsize;         /* Total size of DTB in bytes */
    uint32_t off_dt_struct;     /* Offset to structure block */
    uint32_t off_dt_strings;    /* Offset to strings block */
    uint32_t off_mem_rsvmap;    /* Offset to memory reservation block */
    uint32_t version;           /* Version (17+) */
    uint32_t last_comp_version; /* Last compatible version */
    uint32_t boot_cpuid_phys;   /* Physical ID of boot CPU */
    uint32_t size_dt_strings;   /* Size of strings block */
    uint32_t size_dt_struct;    /* Size of structure block */
} fdt_header_t;

/* ── FDT Token types ── */
#define FDT_BEGIN_NODE  0x00000001
#define FDT_END_NODE    0x00000002
#define FDT_PROP        0x00000003
#define FDT_NOP         0x00000004
#define FDT_END         0x00000009

/* ── Property descriptor (in structure block) ── */
typedef struct __attribute__((packed)) {
    uint32_t len;           /* Length of value (big-endian) */
    uint32_t nameoff;       /* Offset into strings block (big-endian) */
} fdt_prop_t;

/* ── FDT Property value (parsed) ── */
typedef struct {
    const char   *name;     /* Property name (points into DTB strings block) */
    const void   *data;     /* Property data (points into DTB struct block) */
    uint32_t      len;      /* Data length in bytes */
} dt_property_t;

/* ── DT Node (lightweight reference) ── */
typedef struct {
    const char   *name;     /* Node name (points into DTB) */
    uint32_t      offset;   /* Offset within structure block (after FDT_BEGIN_NODE token) */
    uint32_t      depth;    /* Nesting depth (0 = root) */
} dt_node_t;

/* ── DT Node callback (for tree walking) ── */
typedef void (*dt_node_callback_t)(const dt_node_t *node, void *ctx);
typedef void (*dt_prop_callback_t)(const dt_node_t *node,
                                    const dt_property_t *prop, void *ctx);

/* ── Maximum supported values ── */
#define DT_MAX_PATH_DEPTH   16
#define DT_MAX_REG_ENTRIES  8
#define DT_MAX_INTERRUPTS   8

/* ── Parsed "reg" property entry ── */
typedef struct {
    uint64_t base;
    uint64_t size;
} dt_reg_entry_t;

/* ── FDT Parser state ── */
typedef struct {
    const uint8_t  *dtb;            /* DTB base pointer */
    uint32_t        totalsize;      /* Total DTB size */
    const uint8_t  *struct_block;   /* Structure block start */
    uint32_t        struct_size;    /* Structure block size */
    const char     *strings_block;  /* Strings block start */
    uint32_t        strings_size;   /* Strings block size */
    bool            initialized;
} dt_parser_t;

/* ── Public API ── */

/* Initialize the parser with a DTB.
 * dtb_addr: pointer to the Flattened Device Tree Blob in memory.
 * Returns HAL_OK if valid DTB found. */
hal_status_t dt_init(dt_parser_t *dt, const void *dtb_addr);

/* Walk the entire device tree, calling cb for each node */
hal_status_t dt_walk(dt_parser_t *dt, dt_node_callback_t cb, void *ctx);

/* Walk the tree and call prop_cb for each property of each node */
hal_status_t dt_walk_properties(dt_parser_t *dt, dt_prop_callback_t cb, void *ctx);

/* Find a node by path (e.g., "/soc/serial@9000000").
 * Returns HAL_OK if found, populates `node`. */
hal_status_t dt_find_node(dt_parser_t *dt, const char *path, dt_node_t *node);

/* Find a property within a node.
 * `node` must have been obtained from dt_find_node or dt_walk. */
hal_status_t dt_find_property(dt_parser_t *dt, const dt_node_t *node,
                               const char *prop_name, dt_property_t *prop);

/* Parse "compatible" property. Returns pointer to first compatible string
 * within the node. The string may contain multiple NUL-separated entries. */
hal_status_t dt_get_compatible(dt_parser_t *dt, const dt_node_t *node,
                                const char **compatible, uint32_t *total_len);

/* Parse "reg" property into base/size pairs.
 * `parent_addr_cells` and `parent_size_cells` define the format.
 * Returns number of entries parsed. */
uint32_t dt_get_reg(dt_parser_t *dt, const dt_node_t *node,
                     uint32_t addr_cells, uint32_t size_cells,
                     dt_reg_entry_t *entries, uint32_t max);

/* Parse "interrupts" property.
 * Returns number of interrupt entries. */
uint32_t dt_get_interrupts(dt_parser_t *dt, const dt_node_t *node,
                            uint32_t *irqs, uint32_t max);

/* Find all devices matching a compatible string and convert to hal_device_t.
 * Returns number of devices found. */
uint32_t dt_find_devices(dt_parser_t *dt, const char *compatible,
                          hal_device_t *devs, uint32_t max);

/* Get a uint32 property value (assumes big-endian, single cell).
 * Returns default_val if not found. */
uint32_t dt_get_u32(dt_parser_t *dt, const dt_node_t *node,
                     const char *prop_name, uint32_t default_val);

#endif /* ALJEFRA_DRV_DT_PARSER_H */
