// =============================================================================
// AlJefra OS — Minimal SAX-style JSON Parser
// =============================================================================

#ifndef JSON_H
#define JSON_H

#include "netstack.h"

// JSON token types
#define JSON_NONE	0
#define JSON_OBJECT	1	// {
#define JSON_ARRAY	2	// [
#define JSON_STRING	3	// "..."
#define JSON_NUMBER	4	// 123, -123.456
#define JSON_BOOL	5	// true/false
#define JSON_NULL	6	// null

// JSON parser state
typedef struct {
	const char *data;
	u32 len;
	u32 pos;
} json_parser_t;

// Initialize parser
void json_init(json_parser_t *p, const char *data, u32 len);

// Skip whitespace
void json_skip_ws(json_parser_t *p);

// Peek at current character (without advancing)
char json_peek(json_parser_t *p);

// Skip a JSON value (object, array, string, number, bool, null)
void json_skip_value(json_parser_t *p);

// Parse a JSON string value into buf (unescaped)
// Returns length of string, -1 on error
int json_parse_string(json_parser_t *p, char *buf, u32 max_len);

// Parse a JSON number (integer only, for simplicity)
int json_parse_int(json_parser_t *p);

// Parse a JSON boolean (returns 1 for true, 0 for false)
int json_parse_bool(json_parser_t *p);

// Check if current value is null and skip it
int json_is_null(json_parser_t *p);

// Find a key in the current JSON object and position parser at its value
// The parser must be positioned at '{' (or just after it)
// Returns 1 if found, 0 if not found (parser positioned at end of object)
int json_find_key(json_parser_t *p, const char *key);

// Convenience: extract a string value for a given key from a JSON object
// json must point to the start of a JSON object
// Returns length of value, -1 if key not found
int json_get_string(const char *json, u32 json_len, const char *key,
                    char *value, u32 max_len);

#endif
