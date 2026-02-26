// =============================================================================
// AlJefra OS — HTTP/1.1 Client
// =============================================================================

#include "http.h"
#include "json.h"

#define MAX_HTTP_CONNS 4
static http_conn_t http_conns[MAX_HTTP_CONNS];

// Send data over HTTP connection (TLS or plain TCP)
static int http_send(http_conn_t *conn, const void *data, u32 len) {
	if (conn->tls)
		return tls_send(conn->tls, data, len);
	else if (conn->tcp)
		return tcp_send(conn->tcp, data, len);
	return -1;
}

// Receive data from HTTP connection (TLS or plain TCP)
static int http_recv(http_conn_t *conn, void *buf, u32 max_len) {
	if (conn->tls)
		return tls_recv(conn->tls, buf, max_len);
	else if (conn->tcp) {
		// Poll until data available or timeout
		u64 start = net_get_time_ms();
		while (1) {
			int n = tcp_recv(conn->tcp, buf, max_len);
			if (n > 0) return n;
			if (n < 0) return -1;
			net_poll();
			if (net_get_time_ms() - start > 60000)
				return -1;
		}
	}
	return -1;
}

// Buffered read: fill internal buffer from TLS or TCP
static int http_fill_buf(http_conn_t *conn) {
	if (conn->buf_pos < conn->buf_len)
		return (int)(conn->buf_len - conn->buf_pos);	// Still have data

	int n = http_recv(conn, conn->buf, sizeof(conn->buf));
	if (n <= 0)
		return n;

	conn->buf_pos = 0;
	conn->buf_len = (u32)n;
	return n;
}

// Read a single byte from the buffered connection
static int http_read_byte(http_conn_t *conn) {
	if (conn->buf_pos >= conn->buf_len) {
		int n = http_fill_buf(conn);
		if (n <= 0)
			return -1;
	}
	return conn->buf[conn->buf_pos++];
}

// Read a line (up to \r\n) into buf, null-terminated
// Returns line length (excluding \r\n), -1 on error
static int http_read_line(http_conn_t *conn, char *line, u32 max_len) {
	u32 pos = 0;
	while (pos < max_len - 1) {
		int c = http_read_byte(conn);
		if (c < 0)
			return -1;
		if (c == '\r') {
			// Expect \n
			int c2 = http_read_byte(conn);
			if (c2 == '\n')
				break;
			// Shouldn't happen, but handle
			line[pos++] = (char)c;
			if (c2 >= 0)
				line[pos++] = (char)c2;
			continue;
		}
		if (c == '\n')
			break;
		line[pos++] = (char)c;
	}
	line[pos] = '\0';
	return (int)pos;
}

// Parse an integer from string
static u32 parse_uint(const char *s) {
	u32 val = 0;
	while (*s >= '0' && *s <= '9') {
		val = val * 10 + (*s - '0');
		s++;
	}
	return val;
}

// Parse a hex integer from string
static u32 parse_hex(const char *s) {
	u32 val = 0;
	while (1) {
		char c = *s;
		if (c >= '0' && c <= '9') val = (val << 4) | (c - '0');
		else if (c >= 'a' && c <= 'f') val = (val << 4) | (c - 'a' + 10);
		else if (c >= 'A' && c <= 'F') val = (val << 4) | (c - 'A' + 10);
		else break;
		s++;
	}
	return val;
}

// Case-insensitive prefix check
static int starts_with_ci(const char *str, const char *prefix) {
	while (*prefix) {
		char a = *str, b = *prefix;
		if (a >= 'A' && a <= 'Z') a += 32;
		if (b >= 'A' && b <= 'Z') b += 32;
		if (a != b) return 0;
		str++;
		prefix++;
	}
	return 1;
}

// Allocate an HTTP connection
static http_conn_t *http_alloc(void) {
	for (int i = 0; i < MAX_HTTP_CONNS; i++) {
		if (!http_conns[i].tls && !http_conns[i].tcp)
			return &http_conns[i];
	}
	return 0;
}

// Connect to an HTTPS server
http_conn_t *http_connect(const char *host, u16 port) {
	http_conn_t *conn = http_alloc();
	if (!conn)
		return 0;

	net_memset(conn, 0, sizeof(http_conn_t));

	// Resolve hostname
	u32 ip = dns_resolve(host);
	if (ip == 0)
		return 0;

	// Establish TLS connection
	conn->tls = tls_connect(ip, port, host);
	if (!conn->tls) {
		net_memset(conn, 0, sizeof(http_conn_t));
		return 0;
	}

	net_strcpy(conn->host, host);
	conn->port = port;
	return conn;
}

// Connect to a plain HTTP server (no TLS)
http_conn_t *http_connect_plain(const char *host, u16 port) {
	http_conn_t *conn = http_alloc();
	if (!conn)
		return 0;

	net_memset(conn, 0, sizeof(http_conn_t));

	// Resolve hostname or use IP directly
	u32 ip = dns_resolve(host);
	if (ip == 0)
		return 0;

	// Establish plain TCP connection
	conn->tcp = tcp_connect(ip, port);
	if (!conn->tcp) {
		net_memset(conn, 0, sizeof(http_conn_t));
		return 0;
	}

	net_strcpy(conn->host, host);
	conn->port = port;
	return conn;
}

// Connect to a plain HTTP server by IP (no DNS)
http_conn_t *http_connect_plain_ip(u32 ip, u16 port, const char *host) {
	http_conn_t *conn = http_alloc();
	if (!conn)
		return 0;

	net_memset(conn, 0, sizeof(http_conn_t));

	conn->tcp = tcp_connect(ip, port);
	if (!conn->tcp) {
		net_memset(conn, 0, sizeof(http_conn_t));
		return 0;
	}

	net_strcpy(conn->host, host);
	conn->port = port;
	return conn;
}

// Send an HTTP request
int http_request(http_conn_t *conn, const char *method, const char *path,
                 const char *headers, const u8 *body, u32 body_len) {
	if (!conn || (!conn->tls && !conn->tcp))
		return -1;

	// Build request line + headers
	char req[2048];
	int len = 0;

	// Request line
	len += mini_sprintf(req + len, "%s %s HTTP/1.1\r\n", method, path);

	// Host header
	len += mini_sprintf(req + len, "Host: %s\r\n", conn->host);

	// Connection: close (simplifies our life)
	len += mini_sprintf(req + len, "Connection: close\r\n");

	// Content-Length if body present
	if (body && body_len > 0)
		len += mini_sprintf(req + len, "Content-Length: %u\r\n", body_len);

	// Extra headers from caller
	if (headers) {
		u32 hlen = net_strlen(headers);
		net_memcpy(req + len, headers, hlen);
		len += hlen;
	}

	// End of headers
	req[len++] = '\r';
	req[len++] = '\n';

	// Send headers
	int ret = http_send(conn, req, (u32)len);
	if (ret < 0)
		return -1;

	// Send body
	if (body && body_len > 0) {
		ret = http_send(conn, body, body_len);
		if (ret < 0)
			return -1;
	}

	return 0;
}

// Read and parse HTTP response headers
int http_read_response(http_conn_t *conn, http_response_t *resp) {
	if (!conn || (!conn->tls && !conn->tcp) || !resp)
		return -1;

	net_memset(resp, 0, sizeof(http_response_t));
	resp->content_length = 0xFFFFFFFF;	// Unknown

	// Read status line: "HTTP/1.1 200 OK"
	char line[512];
	int len = http_read_line(conn, line, sizeof(line));
	if (len < 0)
		return -1;

	// Parse status code
	// Find first space, then parse number
	char *p = line;
	while (*p && *p != ' ') p++;
	if (*p == ' ') p++;
	resp->status_code = (int)parse_uint(p);

	// Read headers until empty line
	while (1) {
		len = http_read_line(conn, line, sizeof(line));
		if (len < 0)
			return -1;
		if (len == 0)
			break;	// Empty line = end of headers

		// Parse known headers
		if (starts_with_ci(line, "content-length:")) {
			char *v = line + 15;
			while (*v == ' ') v++;
			resp->content_length = parse_uint(v);
		}
		else if (starts_with_ci(line, "transfer-encoding:")) {
			char *v = line + 18;
			while (*v == ' ') v++;
			if (starts_with_ci(v, "chunked"))
				resp->chunked = 1;
		}
		else if (starts_with_ci(line, "content-type:")) {
			char *v = line + 13;
			while (*v == ' ') v++;
			u32 i = 0;
			while (*v && i < sizeof(resp->content_type) - 1)
				resp->content_type[i++] = *v++;
			resp->content_type[i] = '\0';
		}
	}

	return 0;
}

// Internal: read chunk size for chunked encoding
static u32 http_read_chunk_size(http_conn_t *conn) {
	char line[32];
	int len = http_read_line(conn, line, sizeof(line));
	if (len <= 0)
		return 0;
	return parse_hex(line);
}

// Read response body
// For Content-Length: reads up to content_length bytes
// For chunked: reads one chunk at a time
// Returns bytes read, 0 on end, -1 on error
int http_read_body(http_conn_t *conn, http_response_t *resp, u8 *buf, u32 max_len) {
	if (!conn || (!conn->tls && !conn->tcp))
		return -1;

	if (resp->chunked) {
		// Read chunk size
		u32 chunk_size = http_read_chunk_size(conn);
		if (chunk_size == 0) {
			// Read trailing \r\n
			char tmp[4];
			http_read_line(conn, tmp, sizeof(tmp));
			return 0;	// End of body
		}

		// Read chunk data
		u32 to_read = chunk_size < max_len ? chunk_size : max_len;
		u32 total = 0;
		while (total < to_read) {
			// Read from buffer first
			if (conn->buf_pos < conn->buf_len) {
				u32 avail = conn->buf_len - conn->buf_pos;
				u32 copy = (to_read - total) < avail ? (to_read - total) : avail;
				net_memcpy(buf + total, conn->buf + conn->buf_pos, copy);
				conn->buf_pos += copy;
				total += copy;
			} else {
				int n = http_fill_buf(conn);
				if (n <= 0) break;
			}
		}

		// Read trailing \r\n after chunk data
		char tmp[4];
		http_read_line(conn, tmp, sizeof(tmp));

		return (int)total;
	}
	else if (resp->content_length != 0xFFFFFFFF) {
		// Content-Length based
		if (resp->content_length == 0)
			return 0;

		u32 to_read = resp->content_length < max_len ? resp->content_length : max_len;
		u32 total = 0;

		while (total < to_read) {
			if (conn->buf_pos < conn->buf_len) {
				u32 avail = conn->buf_len - conn->buf_pos;
				u32 copy = (to_read - total) < avail ? (to_read - total) : avail;
				net_memcpy(buf + total, conn->buf + conn->buf_pos, copy);
				conn->buf_pos += copy;
				total += copy;
			} else {
				int n = http_fill_buf(conn);
				if (n <= 0) break;
			}
		}

		resp->content_length -= total;
		return (int)total;
	}
	else {
		// Read until connection close
		if (conn->buf_pos < conn->buf_len) {
			u32 avail = conn->buf_len - conn->buf_pos;
			u32 copy = avail < max_len ? avail : max_len;
			net_memcpy(buf, conn->buf + conn->buf_pos, copy);
			conn->buf_pos += copy;
			return (int)copy;
		}

		int n = http_recv(conn, buf, max_len);
		return n > 0 ? n : 0;
	}
}

// Close HTTP connection
void http_close(http_conn_t *conn) {
	if (!conn)
		return;
	if (conn->tls) {
		tls_close(conn->tls);
		conn->tls = 0;
	}
	if (conn->tcp) {
		tcp_close(conn->tcp);
		conn->tcp = 0;
	}
	net_memset(conn, 0, sizeof(http_conn_t));
}
