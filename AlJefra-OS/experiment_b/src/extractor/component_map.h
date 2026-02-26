/*
 * Component Map
 * Maps source files and labels to component IDs
 */
#ifndef COMPONENT_MAP_H
#define COMPONENT_MAP_H

#include "../config.h"
#include "listing_parser.h"

/* Build component regions from a parsed listing and decoded instructions.
 * Returns number of components found. */
int component_map_build(const listing_t *lst,
                        const instruction_t *instructions, int num_instructions,
                        component_region_t *regions, int max_regions);

/* Get the component ID for a source filename */
component_id_t component_from_filename(const char *filename);

/* Get the component ID for a label name */
component_id_t component_from_label(const char *label);

/* Extract a single component's binary from the full kernel binary.
 * Caller must free the returned buffer. */
uint8_t *component_extract_binary(const uint8_t *kernel_bin,
                                  size_t kernel_size,
                                  const component_region_t *region,
                                  size_t *out_size);

#endif /* COMPONENT_MAP_H */
