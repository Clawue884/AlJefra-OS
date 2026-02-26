// =============================================================================
// AlJefra OS — LLM Agent (Ollama + Claude API)
// =============================================================================

#ifndef AGENT_H
#define AGENT_H

#include "netstack.h"

#define AI_MAX_RESPONSE		32768	// 32KB response buffer

// Backend types
#define AI_BACKEND_OLLAMA	0
#define AI_BACKEND_ANTHROPIC	1

// AI agent state
typedef struct {
	int backend;			// AI_BACKEND_OLLAMA or AI_BACKEND_ANTHROPIC
	char model[64];
	u32 host_ip;			// IP address of the LLM server
	u16 host_port;			// Port of the LLM server
	char host_name[128];		// Hostname (for HTTP Host header / TLS SNI)
	char api_key[128];		// API key (Anthropic only)
	u8 response_buf[AI_MAX_RESPONSE];
	u32 response_len;
	int initialized;
} ai_agent_t;

// Initialize agent for Ollama (local LLM)
int ai_init_ollama(ai_agent_t *agent, u32 ip, u16 port, const char *model);

// Initialize agent for Anthropic Claude API
int ai_init_anthropic(ai_agent_t *agent, const char *api_key, const char *model);

// Send a prompt and receive a response
// Returns: length of response text, or -1 on error
int ai_send(ai_agent_t *agent, const char *prompt, char *response, u32 max_len);

#endif
