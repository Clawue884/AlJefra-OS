/* SPDX-License-Identifier: MIT */
/* AlJefra OS — Flattened Device Tree (FDT) Parser Implementation
 * Pure C parser for Device Tree Blobs (DTB).
 */

#include "dt_parser.h"
#include "../../lib/string.h"

/* ── Big-endian to host conversion ── */

static inline uint32_t be32(const void *p)
{
    const uint8_t *b = (const uint8_t *)p;
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}

static inline uint64_t be64(const void *p)
{
    return ((uint64_t)be32(p) << 32) | be32((const uint8_t *)p + 4);
}

/* ── String helpers ── */

/* Compare up to n characters */
static bool dt_strneq(const char *a, const char *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return false;
        if (a[i] == 0) return true;
    }
    return true;
}

/* Check if `haystack` contains `needle` as one of its NUL-separated entries */
static bool dt_compat_match(const char *haystack, uint32_t haystack_len,
                             const char *needle)
{
    uint32_t needle_len = str_len(needle);
    uint32_t pos = 0;
    while (pos < haystack_len) {
        const char *entry = haystack + pos;
        uint32_t entry_len = str_len(entry);
        if (entry_len == needle_len && str_eq(entry, needle))
            return true;
        pos += entry_len + 1;
    }
    return false;
}

/* Align offset to 4-byte boundary */
static inline uint32_t dt_align4(uint32_t offset)
{
    return (offset + 3) & ~3u;
}

/* ── Structure block traversal ── */

/* Get the token at a given offset (in bytes from struct block start) */
static uint32_t dt_token(dt_parser_t *dt, uint32_t offset)
{
    if (offset + 4 > dt->struct_size)
        return FDT_END;
    return be32(dt->struct_block + offset);
}

/* Get the node name string at offset (right after FDT_BEGIN_NODE token) */
static const char *dt_node_name(dt_parser_t *dt, uint32_t offset)
{
    return (const char *)(dt->struct_block + offset);
}

/* Get a string from the strings block */
static const char *dt_string(dt_parser_t *dt, uint32_t nameoff)
{
    if (nameoff >= dt->strings_size)
        return "";
    return dt->strings_block + nameoff;
}

/* Skip past a node name (including NUL and alignment) */
static uint32_t dt_skip_name(dt_parser_t *dt, uint32_t offset)
{
    const char *name = dt_node_name(dt, offset);
    uint32_t len = str_len(name) + 1;  /* Include NUL */
    return dt_align4(offset + len);
}

/* ── Public API ── */

hal_status_t dt_init(dt_parser_t *dt, const void *dtb_addr)
{
    dt->initialized = false;
    dt->dtb = (const uint8_t *)dtb_addr;

    /* Validate magic */
    uint32_t magic = be32(dt->dtb);
    if (magic != FDT_MAGIC)
        return HAL_ERROR;

    const fdt_header_t *hdr = (const fdt_header_t *)dt->dtb;
    dt->totalsize = be32(&hdr->totalsize);

    uint32_t version = be32(&hdr->version);
    if (version < FDT_VERSION)
        return HAL_NOT_SUPPORTED;

    dt->struct_block = dt->dtb + be32(&hdr->off_dt_struct);
    dt->struct_size = be32(&hdr->size_dt_struct);
    dt->strings_block = (const char *)(dt->dtb + be32(&hdr->off_dt_strings));
    dt->strings_size = be32(&hdr->size_dt_strings);

    dt->initialized = true;
    return HAL_OK;
}

hal_status_t dt_walk(dt_parser_t *dt, dt_node_callback_t cb, void *ctx)
{
    if (!dt->initialized)
        return HAL_ERROR;

    uint32_t offset = 0;
    uint32_t depth = 0;

    while (offset < dt->struct_size) {
        uint32_t token = dt_token(dt, offset);
        offset += 4;

        switch (token) {
        case FDT_BEGIN_NODE: {
            dt_node_t node;
            node.name = dt_node_name(dt, offset);
            node.offset = offset;
            node.depth = depth;
            if (cb)
                cb(&node, ctx);
            offset = dt_skip_name(dt, offset);
            depth++;
            break;
        }
        case FDT_END_NODE:
            if (depth > 0) depth--;
            break;
        case FDT_PROP: {
            if (offset + 8 > dt->struct_size) return HAL_ERROR;
            uint32_t prop_len = be32(dt->struct_block + offset);
            offset += 8;  /* Skip len + nameoff */
            offset = dt_align4(offset + prop_len);
            break;
        }
        case FDT_NOP:
            break;
        case FDT_END:
            return HAL_OK;
        default:
            return HAL_ERROR;
        }
    }

    return HAL_OK;
}

hal_status_t dt_walk_properties(dt_parser_t *dt, dt_prop_callback_t cb, void *ctx)
{
    if (!dt->initialized)
        return HAL_ERROR;

    uint32_t offset = 0;
    uint32_t depth = 0;
    dt_node_t current_node;
    current_node.name = "";
    current_node.offset = 0;
    current_node.depth = 0;

    while (offset < dt->struct_size) {
        uint32_t token = dt_token(dt, offset);
        offset += 4;

        switch (token) {
        case FDT_BEGIN_NODE:
            current_node.name = dt_node_name(dt, offset);
            current_node.offset = offset;
            current_node.depth = depth;
            offset = dt_skip_name(dt, offset);
            depth++;
            break;
        case FDT_END_NODE:
            if (depth > 0) depth--;
            break;
        case FDT_PROP: {
            if (offset + 8 > dt->struct_size) return HAL_ERROR;
            uint32_t prop_len = be32(dt->struct_block + offset);
            uint32_t nameoff  = be32(dt->struct_block + offset + 4);
            offset += 8;

            dt_property_t prop;
            prop.name = dt_string(dt, nameoff);
            prop.data = dt->struct_block + offset;
            prop.len = prop_len;

            if (cb)
                cb(&current_node, &prop, ctx);

            offset = dt_align4(offset + prop_len);
            break;
        }
        case FDT_NOP:
            break;
        case FDT_END:
            return HAL_OK;
        default:
            return HAL_ERROR;
        }
    }

    return HAL_OK;
}

hal_status_t dt_find_node(dt_parser_t *dt, const char *path, dt_node_t *node)
{
    if (!dt->initialized)
        return HAL_ERROR;

    /* Parse path into components */
    /* Skip leading '/' */
    const char *p = path;
    if (*p == '/') p++;
    if (*p == '\0') {
        /* Root node */
        node->name = "";
        node->offset = 0;
        node->depth = 0;
        return HAL_OK;
    }

    uint32_t offset = 0;
    uint32_t depth = 0;

    /* Count target depth and get component at each level */
    const char *components[DT_MAX_PATH_DEPTH];
    uint32_t comp_lens[DT_MAX_PATH_DEPTH];
    uint32_t ncomps = 0;

    const char *start = p;
    while (*p && ncomps < DT_MAX_PATH_DEPTH) {
        if (*p == '/') {
            components[ncomps] = start;
            comp_lens[ncomps] = (uint32_t)(p - start);
            ncomps++;
            p++;
            start = p;
        } else {
            p++;
        }
    }
    if (start < p && ncomps < DT_MAX_PATH_DEPTH) {
        components[ncomps] = start;
        comp_lens[ncomps] = (uint32_t)(p - start);
        ncomps++;
    }

    uint32_t match_level = 0;

    while (offset < dt->struct_size) {
        uint32_t token = dt_token(dt, offset);
        offset += 4;

        switch (token) {
        case FDT_BEGIN_NODE: {
            const char *name = dt_node_name(dt, offset);
            uint32_t name_len = str_len(name);

            if (depth == match_level && match_level < ncomps) {
                if (name_len == comp_lens[match_level] &&
                    dt_strneq(name, components[match_level], name_len)) {
                    match_level++;
                    if (match_level == ncomps) {
                        node->name = name;
                        node->offset = offset;
                        node->depth = depth;
                        return HAL_OK;
                    }
                }
            }

            offset = dt_skip_name(dt, offset);
            depth++;
            break;
        }
        case FDT_END_NODE:
            if (depth > 0) {
                depth--;
                if (depth < match_level)
                    match_level = depth;
            }
            break;
        case FDT_PROP: {
            uint32_t prop_len = be32(dt->struct_block + offset);
            offset += 8;
            offset = dt_align4(offset + prop_len);
            break;
        }
        case FDT_NOP:
            break;
        case FDT_END:
            return HAL_NO_DEVICE;
        }
    }

    return HAL_NO_DEVICE;
}

hal_status_t dt_find_property(dt_parser_t *dt, const dt_node_t *node,
                               const char *prop_name, dt_property_t *prop)
{
    if (!dt->initialized)
        return HAL_ERROR;

    /* Navigate to the node's offset and scan its properties */
    uint32_t offset = dt_skip_name(dt, node->offset);
    uint32_t depth = 0;

    while (offset < dt->struct_size) {
        uint32_t token = dt_token(dt, offset);
        offset += 4;

        switch (token) {
        case FDT_BEGIN_NODE:
            offset = dt_skip_name(dt, offset);
            depth++;
            break;
        case FDT_END_NODE:
            if (depth == 0)
                return HAL_NO_DEVICE;  /* Reached end of target node */
            depth--;
            break;
        case FDT_PROP: {
            uint32_t prop_len = be32(dt->struct_block + offset);
            uint32_t nameoff  = be32(dt->struct_block + offset + 4);
            offset += 8;

            if (depth == 0) {
                const char *name = dt_string(dt, nameoff);
                if (str_eq(name, prop_name)) {
                    prop->name = name;
                    prop->data = dt->struct_block + offset;
                    prop->len = prop_len;
                    return HAL_OK;
                }
            }

            offset = dt_align4(offset + prop_len);
            break;
        }
        case FDT_NOP:
            break;
        case FDT_END:
            return HAL_NO_DEVICE;
        }
    }

    return HAL_NO_DEVICE;
}

hal_status_t dt_get_compatible(dt_parser_t *dt, const dt_node_t *node,
                                const char **compatible, uint32_t *total_len)
{
    dt_property_t prop;
    hal_status_t st = dt_find_property(dt, node, "compatible", &prop);
    if (st != HAL_OK)
        return st;

    *compatible = (const char *)prop.data;
    *total_len = prop.len;
    return HAL_OK;
}

uint32_t dt_get_reg(dt_parser_t *dt, const dt_node_t *node,
                     uint32_t addr_cells, uint32_t size_cells,
                     dt_reg_entry_t *entries, uint32_t max)
{
    dt_property_t prop;
    if (dt_find_property(dt, node, "reg", &prop) != HAL_OK)
        return 0;

    uint32_t cell_size = (addr_cells + size_cells) * 4;
    if (cell_size == 0)
        return 0;

    uint32_t count = prop.len / cell_size;
    if (count > max) count = max;

    const uint8_t *data = (const uint8_t *)prop.data;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t off = i * cell_size;

        /* Read address (1 or 2 cells) */
        if (addr_cells == 2)
            entries[i].base = be64(data + off);
        else
            entries[i].base = be32(data + off);
        off += addr_cells * 4;

        /* Read size (1 or 2 cells) */
        if (size_cells == 2)
            entries[i].size = be64(data + off);
        else if (size_cells == 1)
            entries[i].size = be32(data + off);
        else
            entries[i].size = 0;
    }

    return count;
}

uint32_t dt_get_interrupts(dt_parser_t *dt, const dt_node_t *node,
                            uint32_t *irqs, uint32_t max)
{
    dt_property_t prop;
    if (dt_find_property(dt, node, "interrupts", &prop) != HAL_OK)
        return 0;

    uint32_t count = prop.len / 4;
    if (count > max) count = max;

    const uint8_t *data = (const uint8_t *)prop.data;
    for (uint32_t i = 0; i < count; i++)
        irqs[i] = be32(data + i * 4);

    return count;
}

/* Context for dt_find_devices callback */
typedef struct {
    dt_parser_t   *dt;
    const char    *target_compat;
    hal_device_t  *devs;
    uint32_t       max;
    uint32_t       count;
} dt_find_ctx_t;

static void dt_find_devices_cb(const dt_node_t *node, void *ctx)
{
    dt_find_ctx_t *fctx = (dt_find_ctx_t *)ctx;
    if (fctx->count >= fctx->max)
        return;

    const char *compat;
    uint32_t compat_len;
    if (dt_get_compatible(fctx->dt, node, &compat, &compat_len) != HAL_OK)
        return;

    if (!dt_compat_match(compat, compat_len, fctx->target_compat))
        return;

    hal_device_t *dev = &fctx->devs[fctx->count];

    /* Zero out */
    uint8_t *p = (uint8_t *)dev;
    for (uint32_t i = 0; i < sizeof(hal_device_t); i++)
        p[i] = 0;

    dev->bus_type = HAL_BUS_DT;

    /* Copy compatible string */
    uint32_t copy_len = compat_len;
    if (copy_len >= sizeof(dev->compatible))
        copy_len = sizeof(dev->compatible) - 1;
    const char *src = compat;
    for (uint32_t i = 0; i < copy_len; i++)
        dev->compatible[i] = src[i];
    dev->compatible[copy_len] = '\0';

    /* Parse "reg" (assume #address-cells=2, #size-cells=2 for now) */
    dt_reg_entry_t regs[HAL_BUS_MAX_BARS];
    uint32_t nregs = dt_get_reg(fctx->dt, node, 2, 2, regs, HAL_BUS_MAX_BARS);
    for (uint32_t i = 0; i < nregs && i < HAL_BUS_MAX_BARS; i++) {
        dev->bar[i] = regs[i].base;
        dev->bar_size[i] = regs[i].size;
    }

    /* Parse "interrupts" */
    uint32_t irqs[DT_MAX_INTERRUPTS];
    uint32_t nirqs = dt_get_interrupts(fctx->dt, node, irqs, DT_MAX_INTERRUPTS);
    if (nirqs > 0)
        dev->irq = (uint8_t)(irqs[0] & 0xFF);

    fctx->count++;
}

uint32_t dt_find_devices(dt_parser_t *dt, const char *compatible,
                          hal_device_t *devs, uint32_t max)
{
    dt_find_ctx_t ctx;
    ctx.dt = dt;
    ctx.target_compat = compatible;
    ctx.devs = devs;
    ctx.max = max;
    ctx.count = 0;

    dt_walk(dt, dt_find_devices_cb, &ctx);
    return ctx.count;
}

uint32_t dt_get_u32(dt_parser_t *dt, const dt_node_t *node,
                     const char *prop_name, uint32_t default_val)
{
    dt_property_t prop;
    if (dt_find_property(dt, node, prop_name, &prop) != HAL_OK)
        return default_val;
    if (prop.len < 4)
        return default_val;
    return be32(prop.data);
}
