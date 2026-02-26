// =============================================================================
// AlJefra OS — Minimal SAX-style JSON Parser
// =============================================================================

#include "json.h"

void json_init(json_parser_t *p, const char *data, u32 len) {
	p->data = data;
	p->len = len;
	p->pos = 0;
}

void json_skip_ws(json_parser_t *p) {
	while (p->pos < p->len) {
		char c = p->data[p->pos];
		if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
			break;
		p->pos++;
	}
}

char json_peek(json_parser_t *p) {
	json_skip_ws(p);
	if (p->pos >= p->len) return '\0';
	return p->data[p->pos];
}

// Skip a JSON string (from opening " to closing ")
static void json_skip_string(json_parser_t *p) {
	if (p->pos >= p->len || p->data[p->pos] != '"') return;
	p->pos++;	// skip opening "
	while (p->pos < p->len) {
		char c = p->data[p->pos++];
		if (c == '\\') { p->pos++; continue; }	// skip escaped char
		if (c == '"') return;
	}
}

// Skip a JSON number
static void json_skip_number(json_parser_t *p) {
	if (p->pos < p->len && p->data[p->pos] == '-') p->pos++;
	while (p->pos < p->len && p->data[p->pos] >= '0' && p->data[p->pos] <= '9')
		p->pos++;
	if (p->pos < p->len && p->data[p->pos] == '.') {
		p->pos++;
		while (p->pos < p->len && p->data[p->pos] >= '0' && p->data[p->pos] <= '9')
			p->pos++;
	}
	// Exponent
	if (p->pos < p->len && (p->data[p->pos] == 'e' || p->data[p->pos] == 'E')) {
		p->pos++;
		if (p->pos < p->len && (p->data[p->pos] == '+' || p->data[p->pos] == '-'))
			p->pos++;
		while (p->pos < p->len && p->data[p->pos] >= '0' && p->data[p->pos] <= '9')
			p->pos++;
	}
}

void json_skip_value(json_parser_t *p) {
	json_skip_ws(p);
	if (p->pos >= p->len) return;

	char c = p->data[p->pos];

	if (c == '"') {
		json_skip_string(p);
	}
	else if (c == '{') {
		// Object: skip all key-value pairs
		p->pos++;
		json_skip_ws(p);
		if (p->pos < p->len && p->data[p->pos] == '}') { p->pos++; return; }
		while (p->pos < p->len) {
			json_skip_value(p);	// skip key
			json_skip_ws(p);
			if (p->pos < p->len && p->data[p->pos] == ':') p->pos++;
			json_skip_value(p);	// skip value
			json_skip_ws(p);
			if (p->pos < p->len && p->data[p->pos] == ',') { p->pos++; continue; }
			if (p->pos < p->len && p->data[p->pos] == '}') { p->pos++; return; }
			break;
		}
	}
	else if (c == '[') {
		// Array: skip all elements
		p->pos++;
		json_skip_ws(p);
		if (p->pos < p->len && p->data[p->pos] == ']') { p->pos++; return; }
		while (p->pos < p->len) {
			json_skip_value(p);
			json_skip_ws(p);
			if (p->pos < p->len && p->data[p->pos] == ',') { p->pos++; continue; }
			if (p->pos < p->len && p->data[p->pos] == ']') { p->pos++; return; }
			break;
		}
	}
	else if (c == '-' || (c >= '0' && c <= '9')) {
		json_skip_number(p);
	}
	else if (c == 't') {
		p->pos += 4;	// true
	}
	else if (c == 'f') {
		p->pos += 5;	// false
	}
	else if (c == 'n') {
		p->pos += 4;	// null
	}
}

int json_parse_string(json_parser_t *p, char *buf, u32 max_len) {
	json_skip_ws(p);
	if (p->pos >= p->len || p->data[p->pos] != '"')
		return -1;

	p->pos++;	// skip opening "
	u32 out = 0;

	while (p->pos < p->len && out < max_len - 1) {
		char c = p->data[p->pos++];
		if (c == '"') {
			buf[out] = '\0';
			return (int)out;
		}
		if (c == '\\') {
			if (p->pos >= p->len) break;
			c = p->data[p->pos++];
			switch (c) {
			case '"': buf[out++] = '"'; break;
			case '\\': buf[out++] = '\\'; break;
			case '/': buf[out++] = '/'; break;
			case 'n': buf[out++] = '\n'; break;
			case 'r': buf[out++] = '\r'; break;
			case 't': buf[out++] = '\t'; break;
			case 'u': p->pos += 4; buf[out++] = '?'; break;	// Skip \uXXXX
			default: buf[out++] = c; break;
			}
		} else {
			buf[out++] = c;
		}
	}
	buf[out] = '\0';
	return (int)out;
}

int json_parse_int(json_parser_t *p) {
	json_skip_ws(p);
	int neg = 0;
	int val = 0;
	if (p->pos < p->len && p->data[p->pos] == '-') {
		neg = 1;
		p->pos++;
	}
	while (p->pos < p->len && p->data[p->pos] >= '0' && p->data[p->pos] <= '9') {
		val = val * 10 + (p->data[p->pos] - '0');
		p->pos++;
	}
	return neg ? -val : val;
}

int json_parse_bool(json_parser_t *p) {
	json_skip_ws(p);
	if (p->pos < p->len && p->data[p->pos] == 't') {
		p->pos += 4;
		return 1;
	}
	if (p->pos < p->len && p->data[p->pos] == 'f') {
		p->pos += 5;
		return 0;
	}
	return 0;
}

int json_is_null(json_parser_t *p) {
	json_skip_ws(p);
	if (p->pos + 4 <= p->len &&
	    p->data[p->pos] == 'n' && p->data[p->pos+1] == 'u' &&
	    p->data[p->pos+2] == 'l' && p->data[p->pos+3] == 'l') {
		p->pos += 4;
		return 1;
	}
	return 0;
}

// Find a key in the current JSON object
// Parser must be positioned at or just before '{'
int json_find_key(json_parser_t *p, const char *key) {
	json_skip_ws(p);
	if (p->pos >= p->len || p->data[p->pos] != '{')
		return 0;
	p->pos++;	// skip {

	u32 key_len = net_strlen(key);

	while (p->pos < p->len) {
		json_skip_ws(p);
		if (p->data[p->pos] == '}')
			return 0;	// End of object, key not found

		// Parse key string
		if (p->data[p->pos] != '"')
			return 0;

		// Check if this key matches
		u32 start = p->pos + 1;
		json_skip_string(p);
		u32 klen = p->pos - start - 1;	// -1 for closing "

		json_skip_ws(p);
		if (p->pos < p->len && p->data[p->pos] == ':')
			p->pos++;	// skip :

		if (klen == key_len && net_memcmp(p->data + start, key, key_len) == 0) {
			// Found it — parser is positioned at the value
			json_skip_ws(p);
			return 1;
		}

		// Not the key we want — skip value
		json_skip_value(p);
		json_skip_ws(p);
		if (p->pos < p->len && p->data[p->pos] == ',')
			p->pos++;
	}
	return 0;
}

// Convenience: extract string value for a key from JSON object
int json_get_string(const char *json, u32 json_len, const char *key,
                    char *value, u32 max_len) {
	json_parser_t p;
	json_init(&p, json, json_len);
	if (!json_find_key(&p, key))
		return -1;
	return json_parse_string(&p, value, max_len);
}
