// =============================================================================
// AlJefra OS — HTTP/1.1 Client
// =============================================================================

#ifndef HTTP_H
#define HTTP_H

#include "netstack.h"
#include "tls.h"

// HTTP response
typedef struct {
	int status_code;		// 200, 404, etc.
	u32 content_length;		// From Content-Length header (-1 if chunked)
	int chunked;			// Transfer-Encoding: chunked
	char content_type[64];		// Content-Type header value
	// Body is read separately via http_read_body
} http_response_t;

// HTTP connection (wraps TLS or plain TCP)
typedef struct {
	tls_conn_t *tls;		// HTTPS (when non-NULL)
	tcp_conn_t *tcp;		// Plain HTTP (when tls is NULL)
	char host[128];
	u16 port;
	// Read buffer for parsing headers/body
	u8 buf[4096];
	u32 buf_pos;
	u32 buf_len;
} http_conn_t;

// Connect to an HTTPS server (port 443 typically)
http_conn_t *http_connect(const char *host, u16 port);

// Connect to a plain HTTP server (no TLS)
http_conn_t *http_connect_plain(const char *host, u16 port);

// Connect to a plain HTTP server by IP (no DNS lookup)
http_conn_t *http_connect_plain_ip(u32 ip, u16 port, const char *host);

// Send an HTTP request
// method: "GET" or "POST"
// path: URL path (e.g., "/v1/messages")
// headers: extra headers (can be NULL), each terminated by \r\n
// body: request body (can be NULL)
// body_len: length of body
int http_request(http_conn_t *conn, const char *method, const char *path,
                 const char *headers, const u8 *body, u32 body_len);

// Read response headers, parse status code and content info
int http_read_response(http_conn_t *conn, http_response_t *resp);

// Read response body into buffer
// Returns bytes read, 0 on end, -1 on error
int http_read_body(http_conn_t *conn, http_response_t *resp, u8 *buf, u32 max_len);

// Close HTTP connection
void http_close(http_conn_t *conn);

#endif
