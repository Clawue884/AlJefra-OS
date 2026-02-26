// =============================================================================
// AlJefra OS — LLM Agent (Ollama + Claude API)
// =============================================================================

#include "agent.h"
#include "http.h"
#include "json.h"

// Escape a string for JSON (handle quotes, backslashes, newlines, etc.)
static u32 json_escape(char *dst, u32 dst_max, const char *src) {
	u32 pos = 0;
	while (*src && pos < dst_max - 2) {
		char c = *src++;
		switch (c) {
		case '"':  dst[pos++] = '\\'; dst[pos++] = '"'; break;
		case '\\': dst[pos++] = '\\'; dst[pos++] = '\\'; break;
		case '\n': dst[pos++] = '\\'; dst[pos++] = 'n'; break;
		case '\r': dst[pos++] = '\\'; dst[pos++] = 'r'; break;
		case '\t': dst[pos++] = '\\'; dst[pos++] = 't'; break;
		default:   dst[pos++] = c; break;
		}
	}
	dst[pos] = '\0';
	return pos;
}

// Initialize for Ollama backend
int ai_init_ollama(ai_agent_t *agent, u32 ip, u16 port, const char *model) {
	if (!agent || !model)
		return -1;

	net_memset(agent, 0, sizeof(ai_agent_t));
	agent->backend = AI_BACKEND_OLLAMA;
	agent->host_ip = ip;
	agent->host_port = port;
	net_strcpy(agent->model, model);

	// Build host string for HTTP Host header (IP:port)
	mini_sprintf(agent->host_name, "%u.%u.%u.%u:%u",
		(ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
		(ip >> 8) & 0xFF, ip & 0xFF, port);

	agent->initialized = 1;
	return 0;
}

// Initialize for Anthropic Claude API
int ai_init_anthropic(ai_agent_t *agent, const char *api_key, const char *model) {
	if (!agent || !api_key)
		return -1;

	net_memset(agent, 0, sizeof(ai_agent_t));
	agent->backend = AI_BACKEND_ANTHROPIC;
	net_strcpy(agent->api_key, api_key);
	net_strcpy(agent->model, model ? model : "claude-sonnet-4-20250514");
	net_strcpy(agent->host_name, "api.anthropic.com");
	agent->host_port = 443;
	agent->initialized = 1;
	return 0;
}

// Send via Ollama API
// POST /api/chat
// {"model":"...","messages":[{"role":"user","content":"..."}],"stream":false}
// Response: {"message":{"role":"assistant","content":"..."},"done":true}
static int ai_send_ollama(ai_agent_t *agent, const char *prompt, char *response, u32 max_len) {
	// Connect via plain HTTP
	http_conn_t *conn = http_connect_plain_ip(agent->host_ip, agent->host_port, agent->host_name);
	if (!conn) {
		net_strcpy(response, "[error: cannot connect to Ollama]");
		return -1;
	}

	// Build JSON request body
	char body[8192];
	char escaped_prompt[4096];
	json_escape(escaped_prompt, sizeof(escaped_prompt), prompt);

	int body_len = mini_sprintf(body,
		"{\"model\":\"%s\",\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}],\"stream\":false}",
		agent->model, escaped_prompt);

	// Send request
	char headers[] = "Content-Type: application/json\r\n";
	int ret = http_request(conn, "POST", "/api/chat", headers, (u8 *)body, (u32)body_len);
	if (ret < 0) {
		http_close(conn);
		net_strcpy(response, "[error: failed to send request]");
		return -1;
	}

	// Read response headers
	http_response_t resp;
	ret = http_read_response(conn, &resp);
	if (ret < 0) {
		http_close(conn);
		net_strcpy(response, "[error: failed to read response headers]");
		return -1;
	}

	// Read full response body
	u8 *buf = agent->response_buf;
	u32 total = 0;
	while (total < AI_MAX_RESPONSE - 1) {
		int n = http_read_body(conn, &resp, buf + total, AI_MAX_RESPONSE - 1 - total);
		if (n <= 0)
			break;
		total += (u32)n;
	}
	buf[total] = '\0';
	agent->response_len = total;

	http_close(conn);

	// Check HTTP status
	if (resp.status_code != 200) {
		int len = mini_sprintf(response, "HTTP %d: %s", resp.status_code, (char *)buf);
		if ((u32)len >= max_len) len = max_len - 1;
		response[len] = '\0';
		return -1;
	}

	// Parse Ollama response: {"message":{"role":"...","content":"..."},...}
	json_parser_t jp;
	json_init(&jp, (char *)buf, total);

	if (json_find_key(&jp, "message")) {
		// Now positioned at the message object
		if (json_find_key(&jp, "content")) {
			int text_len = json_parse_string(&jp, response, max_len);
			if (text_len >= 0)
				return text_len;
		}
	}

	// Fallback: search for "content":" pattern
	const char *needle = "\"content\":\"";
	u32 nlen = 11;
	for (u32 i = 0; i + nlen < total; i++) {
		if (net_memcmp(buf + i, needle, nlen) == 0) {
			json_init(&jp, (char *)buf + i + nlen - 1, total - i - nlen + 1);
			int text_len = json_parse_string(&jp, response, max_len);
			if (text_len >= 0)
				return text_len;
		}
	}

	// If all parsing fails, return raw body
	u32 copy = total < max_len - 1 ? total : max_len - 1;
	net_memcpy(response, buf, copy);
	response[copy] = '\0';
	return (int)copy;
}

// Send via Anthropic Claude API
static int ai_send_anthropic(ai_agent_t *agent, const char *prompt, char *response, u32 max_len) {
	http_conn_t *conn = http_connect("api.anthropic.com", 443);
	if (!conn) {
		net_strcpy(response, "[error: cannot connect to Claude API]");
		return -1;
	}

	// Build JSON request body
	char body[8192];
	char escaped_prompt[4096];
	json_escape(escaped_prompt, sizeof(escaped_prompt), prompt);

	int body_len = mini_sprintf(body,
		"{\"model\":\"%s\",\"max_tokens\":4096,\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}]}",
		agent->model, escaped_prompt);

	// Build extra headers
	char headers[512];
	mini_sprintf(headers,
		"Content-Type: application/json\r\n"
		"x-api-key: %s\r\n"
		"anthropic-version: 2023-06-01\r\n",
		agent->api_key);

	int ret = http_request(conn, "POST", "/v1/messages", headers, (u8 *)body, (u32)body_len);
	if (ret < 0) {
		http_close(conn);
		net_strcpy(response, "[error: failed to send request]");
		return -1;
	}

	// Read response
	http_response_t resp;
	ret = http_read_response(conn, &resp);
	if (ret < 0) {
		http_close(conn);
		net_strcpy(response, "[error: failed to read response]");
		return -1;
	}

	u8 *buf = agent->response_buf;
	u32 total = 0;
	while (total < AI_MAX_RESPONSE - 1) {
		int n = http_read_body(conn, &resp, buf + total, AI_MAX_RESPONSE - 1 - total);
		if (n <= 0) break;
		total += (u32)n;
	}
	buf[total] = '\0';
	agent->response_len = total;
	http_close(conn);

	if (resp.status_code != 200) {
		int len = mini_sprintf(response, "HTTP %d: %s", resp.status_code, (char *)buf);
		if ((u32)len >= max_len) len = max_len - 1;
		response[len] = '\0';
		return -1;
	}

	// Parse Claude response: {"content":[{"type":"text","text":"..."}],...}
	// Search for "text":" pattern
	const char *needle = "\"text\":\"";
	u32 nlen = 8;
	for (u32 i = 0; i + nlen < total; i++) {
		if (net_memcmp(buf + i, needle, nlen) == 0) {
			json_parser_t jp;
			json_init(&jp, (char *)buf + i + nlen - 1, total - i - nlen + 1);
			int text_len = json_parse_string(&jp, response, max_len);
			if (text_len >= 0)
				return text_len;
		}
	}

	net_strcpy(response, "[error: cannot parse Claude response]");
	return -1;
}

// Send a prompt to the configured LLM backend
int ai_send(ai_agent_t *agent, const char *prompt, char *response, u32 max_len) {
	if (!agent || !agent->initialized || !prompt || !response)
		return -1;

	if (agent->backend == AI_BACKEND_OLLAMA)
		return ai_send_ollama(agent, prompt, response, max_len);
	else
		return ai_send_anthropic(agent, prompt, response, max_len);
}
