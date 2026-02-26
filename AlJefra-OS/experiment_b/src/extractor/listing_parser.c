/*
 * NASM Listing File Parser
 * Format: "   <line> <address> <hex_bytes>  <source>"
 * Example: "    42 00100030 48C7C000001000      mov rax, 0x100000"
 */
#include "listing_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define INITIAL_CAPACITY 4096

/* Try to parse hex bytes from a listing line segment */
static int parse_hex_bytes(const char *hex, uint8_t *out, int max_bytes) {
    int count = 0;
    while (*hex && count < max_bytes) {
        while (isspace((unsigned char)*hex)) hex++;
        if (!isxdigit((unsigned char)*hex)) break;

        char buf[3] = {hex[0], 0, 0};
        if (isxdigit((unsigned char)hex[1])) {
            buf[1] = hex[1];
            hex += 2;
        } else {
            hex += 1;
        }
        out[count++] = (uint8_t)strtoul(buf, NULL, 16);
    }
    return count;
}

/* Check if a source line defines a label */
static int extract_label(const char *source, char *label, int max_len) {
    const char *s = source;

    /* Skip leading whitespace */
    while (isspace((unsigned char)*s)) s++;

    /* A label starts at column 0 (or after whitespace) and ends with ':' */
    /* Or it's a plain identifier at column 0 without indentation */
    if (*s == ';' || *s == '%' || *s == '\0') return 0;

    const char *start = s;
    while (*s && *s != ':' && *s != ' ' && *s != '\t' && *s != ';') s++;

    if (*s == ':') {
        int len = (int)(s - start);
        if (len > 0 && len < max_len) {
            memcpy(label, start, len);
            label[len] = '\0';

            /* Reject local labels (start with .) */
            if (label[0] == '.') return 0;

            return 1;
        }
    }

    return 0;
}

int listing_parse(const char *listing_path, listing_t *out) {
    FILE *f = fopen(listing_path, "r");
    if (!f) {
        fprintf(stderr, "Error: Cannot open listing file: %s\n", listing_path);
        return -1;
    }

    out->lines = malloc(INITIAL_CAPACITY * sizeof(listing_line_t));
    if (!out->lines) {
        fclose(f);
        return -1;
    }
    out->capacity = INITIAL_CAPACITY;
    out->num_lines = 0;
    out->current_file[0] = '\0';

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        /* Remove trailing newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        /* Grow array if needed */
        if (out->num_lines >= out->capacity) {
            out->capacity *= 2;
            out->lines = realloc(out->lines,
                                 out->capacity * sizeof(listing_line_t));
            if (!out->lines) {
                fclose(f);
                return -1;
            }
        }

        listing_line_t *ll = &out->lines[out->num_lines];
        memset(ll, 0, sizeof(*ll));

        /* Track %include directives for source file mapping */
        const char *inc = strstr(line, "%include");
        if (inc) {
            const char *q1 = strchr(inc, '"');
            if (q1) {
                q1++;
                const char *q2 = strchr(q1, '"');
                if (q2) {
                    int flen = (int)(q2 - q1);
                    if (flen < 255) {
                        memcpy(out->current_file, q1, flen);
                        out->current_file[flen] = '\0';
                    }
                }
            }
        }

        /*
         * NASM listing format:
         * "     <line> <address> <hex>                   <source>"
         * The line number is right-justified in columns 1-6
         * Address is 8 hex chars starting around column 7
         * Hex bytes follow
         * Source is after sufficient spacing
         *
         * Real format varies, so we parse flexibly.
         */
        char *p = line;

        /* Skip leading whitespace */
        while (isspace((unsigned char)*p)) p++;

        /* Parse line number */
        if (!isdigit((unsigned char)*p)) {
            /* Not a listing line (could be a section header) */
            out->num_lines++;
            strncpy(ll->source, line, MAX_SOURCE_LINE_LEN - 1);
            continue;
        }

        ll->line_number = (uint32_t)strtoul(p, &p, 10);

        /* Skip whitespace */
        while (isspace((unsigned char)*p)) p++;

        /* Handle include depth markers like <2> */
        if (*p == '<') {
            while (*p && *p != '>') p++;
            if (*p == '>') p++;
            while (isspace((unsigned char)*p)) p++;
            /* Re-parse line number from include */
            if (isdigit((unsigned char)*p)) {
                ll->line_number = (uint32_t)strtoul(p, &p, 10);
                while (isspace((unsigned char)*p)) p++;
            }
        }

        /* Parse address (hex) — 8 hex chars like 00000050 */
        if (isxdigit((unsigned char)*p)) {
            char *end;
            uint64_t addr = strtoull(p, &end, 16);

            /* Must be at least 4 hex chars to be an address, not a line number */
            if (end - p >= 4) {
                ll->address = addr + KERNEL_BASE_ADDR;
                ll->has_code = 1;
                p = end;

                /* Skip whitespace */
                while (isspace((unsigned char)*p)) p++;

                /* Parse hex bytes */
                char hex_part[64] = {0};
                int hi = 0;
                while (isxdigit((unsigned char)*p) && hi < 63) {
                    hex_part[hi++] = *p++;
                }
                hex_part[hi] = '\0';

                if (hi > 0) {
                    ll->num_bytes = parse_hex_bytes(hex_part, ll->bytes,
                                                    MAX_INSTRUCTION_LEN);
                }

                /* Consume the dash for continuation lines */
                if (*p == '-') p++;
            }
        }

        /* Skip whitespace to get source */
        while (isspace((unsigned char)*p)) p++;

        /* Rest is source code */
        strncpy(ll->source, p, MAX_SOURCE_LINE_LEN - 1);

        /* Check for label — labels can appear on non-code lines too */
        ll->is_label = extract_label(ll->source, ll->label, MAX_LABEL_LEN);

        /* Store current filename */
        strncpy(ll->filename, out->current_file, sizeof(ll->filename) - 1);

        out->num_lines++;
    }

    fclose(f);
    return 0;
}

void listing_free(listing_t *lst) {
    if (lst->lines) {
        free(lst->lines);
        lst->lines = NULL;
    }
    lst->num_lines = 0;
    lst->capacity = 0;
}

const listing_line_t *listing_find_address(const listing_t *lst, uint64_t addr) {
    /* Binary search would be faster, but the listing is small enough */
    for (int i = 0; i < lst->num_lines; i++) {
        if (lst->lines[i].has_code && lst->lines[i].address == addr) {
            return &lst->lines[i];
        }
    }
    return NULL;
}

int listing_get_labels(const listing_t *lst, char labels[][MAX_LABEL_LEN],
                       uint64_t *addresses, int max_labels) {
    int count = 0;
    for (int i = 0; i < lst->num_lines && count < max_labels; i++) {
        if (lst->lines[i].is_label) {
            strncpy(labels[count], lst->lines[i].label, MAX_LABEL_LEN - 1);

            /* If the label line has an address, use it.
             * Otherwise, find the next line that has code. */
            if (lst->lines[i].has_code) {
                addresses[count] = lst->lines[i].address;
            } else {
                addresses[count] = 0;
                for (int j = i + 1; j < lst->num_lines; j++) {
                    if (lst->lines[j].has_code) {
                        addresses[count] = lst->lines[j].address;
                        break;
                    }
                }
            }

            if (addresses[count] != 0) {
                count++;
            }
        }
    }
    return count;
}

const char *listing_get_source_file(const listing_t *lst, uint64_t addr) {
    const listing_line_t *ll = listing_find_address(lst, addr);
    if (ll && ll->filename[0]) {
        return ll->filename;
    }
    return "kernel.asm";
}
