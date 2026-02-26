// =============================================================================
// AlJefra OS — Main Entry Point
//
// Boots the network stack, connects to LLM (Ollama on host or Claude API).
// This is the "hello world" for the AI-native OS.
// =============================================================================

#include "netstack.h"
#include "tls.h"
#include "http.h"
#include "json.h"
#include "agent.h"

// Network configuration (QEMU TAP)
#define MY_IP		IP4(10, 0, 0, 2)
#define MY_GATEWAY	IP4(10, 0, 0, 1)
#define MY_NETMASK	IP4(255, 255, 255, 0)
#define MY_DNS		IP4(8, 8, 8, 8)

// Ollama configuration (host machine)
#define OLLAMA_IP	IP4(10, 0, 0, 1)
#define OLLAMA_PORT	11434
#define OLLAMA_MODEL	"llama3.1"

static void print(const char *s) {
	b_output(s, net_strlen(s));
}

static void println(const char *s) {
	print(s);
	print("\n");
}

int main(void) {
	println("AlJefra OS — Self-Bootstrapping AI-Native OS");
	println("================================================");
	println("");

	// Initialize network stack
	print("Initializing network... ");
	if (net_init(MY_IP, MY_GATEWAY, MY_NETMASK, MY_DNS) != 0) {
		println("FAILED");
		return -1;
	}
	println("OK");

	char buf[256];
	net_state_t *ns = net_get_state();
	mini_sprintf(buf, "IP: %u.%u.%u.%u  GW: %u.%u.%u.%u",
		(MY_IP >> 24) & 0xFF, (MY_IP >> 16) & 0xFF,
		(MY_IP >> 8) & 0xFF, MY_IP & 0xFF,
		(MY_GATEWAY >> 24) & 0xFF, (MY_GATEWAY >> 16) & 0xFF,
		(MY_GATEWAY >> 8) & 0xFF, MY_GATEWAY & 0xFF);
	println(buf);

	// ARP resolve gateway
	print("Resolving gateway... ");
	u8 gw_mac[6];
	int tries = 0;
	while (arp_resolve(ns, MY_GATEWAY, gw_mac) != 0) {
		for (int i = 0; i < 200; i++) net_poll();
		if (++tries > 50) { println("FAILED"); return -1; }
	}
	println("OK");

	// Ping gateway
	print("Ping gateway... ");
	if (icmp_ping(MY_GATEWAY, 2000) == 0)
		println("OK");
	else
		println("no reply (continuing)");
	println("");

	// Initialize AI agent (Ollama on host)
	ai_agent_t agent;
	mini_sprintf(buf, "Connecting to Ollama (%u.%u.%u.%u:%u, model: %s)...",
		(OLLAMA_IP >> 24) & 0xFF, (OLLAMA_IP >> 16) & 0xFF,
		(OLLAMA_IP >> 8) & 0xFF, OLLAMA_IP & 0xFF,
		OLLAMA_PORT, OLLAMA_MODEL);
	println(buf);

	if (ai_init_ollama(&agent, OLLAMA_IP, OLLAMA_PORT, OLLAMA_MODEL) != 0) {
		println("Failed to initialize AI agent");
		return -1;
	}

	// First message
	println("");
	println("Sending: \"Hello! I am AlJefra OS, a bare-metal x86-64 OS that just");
	println("booted, built its own TCP/IP stack, and connected to you over HTTP.");
	println("Respond in 2-3 sentences.\"");
	println("");

	char response[8192];
	print("Thinking... ");
	int len = ai_send(&agent,
		"Hello! I am AlJefra OS, a bare-metal x86-64 operating system. "
		"I just booted from assembly, built my own TCP/IP stack in C, and "
		"connected to you over HTTP. Respond in 2-3 sentences.",
		response, sizeof(response));

	if (len > 0) {
		println("OK");
		println("");
		println("Response:");
		println("------------------");
		print(response);
		println("");
		println("------------------");
		println("");
		println("AI-native OS bootstrap complete!");
	} else {
		println("FAILED");
		print("Error: ");
		println(response);
	}

	// Interactive REPL
	println("");
	println("Interactive mode. Type a message and press Enter.");
	println("Type 'quit' to exit.");
	println("");

	char input[1024];
	while (1) {
		print("> ");

		u32 input_pos = 0;
		while (input_pos < sizeof(input) - 1) {
			u8 c = b_input();
			if (c == 0) {
				net_poll();
				continue;
			}
			if (c == '\r' || c == '\n' || c == 0x1C) {
				input[input_pos] = '\0';
				println("");
				break;
			}
			if (c == 8 || c == 127) {
				if (input_pos > 0) {
					input_pos--;
					print("\b \b");
				}
				continue;
			}
			input[input_pos++] = (char)c;
			char echo[2] = {(char)c, 0};
			print(echo);
		}

		if (input_pos == 0) continue;

		if (net_strcmp(input, "quit") == 0 || net_strcmp(input, "exit") == 0)
			break;

		print("Thinking... ");
		len = ai_send(&agent, input, response, sizeof(response));
		if (len > 0) {
			println("OK");
			println("");
			print(response);
			println("");
		} else {
			println("FAILED");
			print("Error: ");
			println(response);
		}
		println("");
	}

	println("Shutting down. Goodbye!");
	return 0;
}
