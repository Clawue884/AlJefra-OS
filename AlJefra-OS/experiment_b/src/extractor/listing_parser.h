/*
 * NASM Listing File Parser
 * Parses NASM -l output to map binary bytes ↔ source lines
 */
#ifndef LISTING_PARSER_H
#define LISTING_PARSER_H

#include "../config.h"

/* A single line from the NASM listing */
typedef struct {
    uint32_t line_number;           /* Source line number */
    uint64_t address;               /* Address if code was generated */
    uint8_t  bytes[MAX_INSTRUCTION_LEN];
    uint8_t  num_bytes;             /* Bytes generated on this line */
    char     source[MAX_SOURCE_LINE_LEN];
    char     label[MAX_LABEL_LEN];  /* Label defined on this line (if any) */
    char     filename[256];         /* Source file (from %include) */
    int      has_code;              /* 1 if this line generated code */
    int      is_label;              /* 1 if this line defines a label */
} listing_line_t;

/* Parsed listing file */
typedef struct {
    listing_line_t *lines;
    int             num_lines;
    int             capacity;
    char            current_file[256];  /* Current source file being parsed */
} listing_t;

/* Parse a NASM listing file.
 * Call listing_free() when done. */
int listing_parse(const char *listing_path, listing_t *out);

/* Free a parsed listing */
void listing_free(listing_t *lst);

/* Find the listing line that contains a given address */
const listing_line_t *listing_find_address(const listing_t *lst, uint64_t addr);

/* Get all labels (function boundaries) from the listing */
int listing_get_labels(const listing_t *lst, char labels[][MAX_LABEL_LEN],
                       uint64_t *addresses, int max_labels);

/* Get the source file for a given address */
const char *listing_get_source_file(const listing_t *lst, uint64_t addr);

#endif /* LISTING_PARSER_H */
