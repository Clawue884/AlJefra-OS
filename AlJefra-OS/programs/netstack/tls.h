// =============================================================================
// AlJefra OS — TLS Wrapper (BearSSL)
// =============================================================================

#ifndef TLS_H
#define TLS_H

#include "netstack.h"
#include "bearssl.h"

// TLS connection wrapping a TCP connection
typedef struct {
	tcp_conn_t *tcp;		// Underlying TCP connection
	br_ssl_client_context sc;	// BearSSL client context
	br_x509_minimal_context xc;	// X.509 certificate validator
	br_sslio_context ioc;		// BearSSL I/O context
	u8 iobuf[BR_SSL_BUFSIZE_BIDI];	// I/O buffer (~33KB)
	int initialized;
} tls_conn_t;

// Initialize TLS subsystem (call once)
void tls_init(void);

// Connect to a TLS server
// Returns pointer to TLS connection, or NULL on failure
tls_conn_t *tls_connect(u32 ip, u16 port, const char *server_name);

// Send data over TLS
int tls_send(tls_conn_t *conn, const void *data, u32 len);

// Receive data over TLS (blocking until data available or timeout)
int tls_recv(tls_conn_t *conn, void *buf, u32 max_len);

// Close TLS connection
void tls_close(tls_conn_t *conn);

// Get last BearSSL error code
int tls_get_error(tls_conn_t *conn);

// Embedded root CA certificates
extern const br_x509_trust_anchor TAs[];
extern const int TAs_NUM;

#endif
