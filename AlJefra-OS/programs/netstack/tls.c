// =============================================================================
// AlJefra OS — TLS Integration (BearSSL)
// =============================================================================

#include "tls.h"

// Maximum simultaneous TLS connections
#define MAX_TLS_CONNS 1

static tls_conn_t tls_conns[MAX_TLS_CONNS];

// Check if RDRAND is supported (CPUID.01H:ECX bit 30)
static int has_rdrand(void) {
	u32 ecx;
	asm volatile (
		"mov $1, %%eax\n\t"
		"cpuid"
		: "=c" (ecx)
		:
		: "eax", "ebx", "edx"
	);
	return (ecx >> 30) & 1;
}

// RDRAND instruction for entropy (only call if has_rdrand() == 1)
static int rdrand64(u64 *val) {
	u8 ok;
	asm volatile (
		"rdrand %0\n\t"
		"setc %1"
		: "=r" (*val), "=qm" (ok)
	);
	return ok;
}

// Seed BearSSL with entropy from RDRAND + RDTSC
static void tls_seed_entropy(br_ssl_engine_context *eng) {
	u8 seed[32];
	int pos = 0;

	// Try RDRAND first (available on Ivy Bridge+, Ryzen+)
	if (has_rdrand()) {
		for (int i = 0; i < 4 && pos < 32; i++) {
			u64 rval;
			if (rdrand64(&rval)) {
				for (int j = 0; j < 8 && pos < 32; j++)
					seed[pos++] = (u8)(rval >> (j * 8));
			}
		}
	}

	// Mix in TSC for additional entropy
	if (pos < 32) {
		u64 tsc = b_system(TSC, 0, 0);
		for (int j = 0; j < 8 && pos < 32; j++)
			seed[pos++] = (u8)(tsc >> (j * 8));
	}

	// Fill remaining with TIMECOUNTER (nanosecond resolution)
	while (pos < 32) {
		u64 t = b_system(TIMECOUNTER, 0, 0);
		seed[pos++] = (u8)(t ^ (t >> 8));
	}

	br_ssl_engine_inject_entropy(eng, seed, sizeof(seed));
}

// Low-level read callback for BearSSL I/O
// Reads from TCP into BearSSL's input buffer
static int tls_low_read(void *ctx, unsigned char *buf, size_t len) {
	tcp_conn_t *tcp = (tcp_conn_t *)ctx;
	net_state_t *ns = net_get_state();

	// Try to read data, polling until some is available or timeout
	u64 start = net_get_time_ms();
	while (1) {
		int n = tcp_recv(tcp, buf, (u32)len);
		if (n > 0)
			return n;
		if (n < 0)
			return -1;	// Connection closed or error

		net_poll();

		// 30-second read timeout
		if (net_get_time_ms() - start > 30000)
			return -1;
	}
}

// Low-level write callback for BearSSL I/O
static int tls_low_write(void *ctx, const unsigned char *buf, size_t len) {
	tcp_conn_t *tcp = (tcp_conn_t *)ctx;
	return tcp_send(tcp, buf, (u32)len);
}

// Initialize TLS subsystem
void tls_init(void) {
	net_memset(tls_conns, 0, sizeof(tls_conns));
}

// Allocate a TLS connection slot
static tls_conn_t *tls_alloc(void) {
	for (int i = 0; i < MAX_TLS_CONNS; i++) {
		if (!tls_conns[i].initialized)
			return &tls_conns[i];
	}
	return 0;
}

// Custom time function for X.509 validation
// Returns a compile-time date since AlJefra OS has no RTC/NTP
// BearSSL expects days since January 1st, 0 AD (Gregorian calendar)
// Unix epoch offset: 719528 days from year 0 to 1970-01-01
static u32 tls_get_days(void) {
	// 2026-02-25: Unix epoch day 20509 + 719528 = 740037
	return 740037;
}

static u32 tls_get_seconds(void) {
	return 43200;	// Noon
}

// Debug print helper
static void tls_dbg(const char *s) {
	b_output(s, net_strlen(s));
}

// Get BearSSL engine error code (for diagnostics)
int tls_get_error(tls_conn_t *conn) {
	if (!conn) return -1;
	return br_ssl_engine_last_error(&conn->sc.eng);
}

// Connect to a TLS server
tls_conn_t *tls_connect(u32 ip, u16 port, const char *server_name) {
	tls_conn_t *tls = tls_alloc();
	if (!tls)
		return 0;

	net_memset(tls, 0, sizeof(tls_conn_t));

	// First establish TCP connection
	tls->tcp = tcp_connect(ip, port);
	if (!tls->tcp)
		return 0;

	// Initialize SSL client context with full cipher suite support
	br_ssl_client_init_full(&tls->sc, &tls->xc, TAs, TAs_NUM);

	// Set time for certificate validation
	br_x509_minimal_set_time(&tls->xc, tls_get_days(), tls_get_seconds());

	// Seed entropy
	tls_seed_entropy(&tls->sc.eng);

	// Set the I/O buffer
	br_ssl_engine_set_buffer(&tls->sc.eng, tls->iobuf, sizeof(tls->iobuf), 1);

	// Reset the client context for a new connection
	if (!br_ssl_client_reset(&tls->sc, server_name, 0))
		return 0;

	// Initialize BearSSL I/O wrapper
	br_sslio_init(&tls->ioc, &tls->sc.eng,
	              tls_low_read, tls->tcp,
	              tls_low_write, tls->tcp);

	tls->initialized = 1;
	return tls;
}

// Send data over TLS
int tls_send(tls_conn_t *conn, const void *data, u32 len) {
	if (!conn || !conn->initialized)
		return -1;

	int ret = br_sslio_write_all(&conn->ioc, data, len);
	if (ret < 0) return -1;

	// Flush the output
	br_sslio_flush(&conn->ioc);

	return (int)len;
}

// Receive data over TLS
int tls_recv(tls_conn_t *conn, void *buf, u32 max_len) {
	if (!conn || !conn->initialized)
		return -1;

	return br_sslio_read(&conn->ioc, buf, max_len);
}

// Close TLS connection
void tls_close(tls_conn_t *conn) {
	if (!conn || !conn->initialized)
		return;

	// Send TLS close_notify
	br_sslio_close(&conn->ioc);

	// Close TCP
	tcp_close(conn->tcp);

	conn->initialized = 0;
}
