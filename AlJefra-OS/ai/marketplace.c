/* SPDX-License-Identifier: MIT */
/* AlJefra OS — Driver Marketplace Client Implementation
 *
 * Connects to the AlJefra Driver Store API via TCP/HTTP.
 * In local mode, connects to gateway_ip:8080 (QEMU host).
 *
 * Protocol:
 *   POST /v1/manifest   → send hardware manifest, get recommendations
 *   GET  /v1/drivers/<vendor>/<device>/<arch>  → download .ajdrv
 */

#include "marketplace.h"
#include "../hal/hal.h"
#include "../net/tcp.h"
#include "../lib/string.h"

/* ── Integer to hex string ── */
static void u16_to_hex(uint16_t v, char *out)
{
    const char hex[] = "0123456789abcdef";
    out[0] = hex[(v >> 12) & 0xF];
    out[1] = hex[(v >> 8) & 0xF];
    out[2] = hex[(v >> 4) & 0xF];
    out[3] = hex[v & 0xF];
    out[4] = '\0';
}

/* ── Integer to decimal string ── */
static int int_to_str(int v, char *buf)
{
    if (v == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return 1;
    }
    char tmp[12];
    int ti = 0;
    int neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    while (v > 0) { tmp[ti++] = '0' + (v % 10); v /= 10; }
    int ni = 0;
    if (neg) buf[ni++] = '-';
    while (ti > 0) buf[ni++] = tmp[--ti];
    buf[ni] = '\0';
    return ni;
}

/* ── IP address formatting ── */
static void ip_to_str(uint32_t ip, char *buf)
{
    char *p = buf;
    for (int i = 3; i >= 0; i--) {
        uint8_t octet = (ip >> (i * 8)) & 0xFF;
        if (octet >= 100) *p++ = '0' + (octet / 100);
        if (octet >= 10) *p++ = '0' + ((octet / 10) % 10);
        *p++ = '0' + (octet % 10);
        if (i > 0) *p++ = '.';
    }
    *p = '\0';
}

/* ── Connection state ── */
static tcp_conn_t g_tcp_conn;
static int g_connected;
static uint32_t g_gateway_ip;

/* ── Build JSON manifest ── */
static int build_json_manifest(const hardware_manifest_t *m, char *buf, uint32_t max)
{
    char *p = buf;
    char *end = buf + max - 1;

    #define APPEND(s) do { \
        const char *_s = (s); \
        while (*_s && p < end) *p++ = *_s++; \
    } while(0)

    APPEND("{\"arch\":");
    switch (m->arch) {
    case HAL_ARCH_X86_64:  APPEND("\"x86_64\""); break;
    case HAL_ARCH_AARCH64: APPEND("\"aarch64\""); break;
    case HAL_ARCH_RISCV64: APPEND("\"riscv64\""); break;
    }

    APPEND(",\"cpu_vendor\":\"");
    APPEND(m->cpu_vendor);
    APPEND("\",\"cpu_model\":\"");
    APPEND(m->cpu_model);

    APPEND("\",\"ram_mb\":");
    uint32_t ram_mb = (uint32_t)(m->ram_bytes / (1024 * 1024));
    char numbuf[12];
    int_to_str((int)ram_mb, numbuf);
    APPEND(numbuf);

    APPEND(",\"devices\":[");

    for (uint32_t i = 0; i < m->entry_count; i++) {
        if (i > 0) APPEND(",");
        APPEND("{\"v\":\"");
        char hex[5];
        u16_to_hex(m->entries[i].vendor_id, hex);
        APPEND(hex);
        APPEND("\",\"d\":\"");
        u16_to_hex(m->entries[i].device_id, hex);
        APPEND(hex);
        APPEND("\",\"c\":");
        int_to_str(m->entries[i].class_code, numbuf);
        APPEND(numbuf);
        APPEND(",\"s\":");
        int_to_str(m->entries[i].subclass, numbuf);
        APPEND(numbuf);
        APPEND(",\"has_drv\":");
        APPEND(m->entries[i].has_driver ? "true" : "false");
        APPEND("}");
    }

    APPEND("]}");
    *p = '\0';

    #undef APPEND
    return (int)(p - buf);
}

/* ── HTTP request builder ── */
static int build_http_request(const char *method, const char *path,
                               const char *host_str,
                               const char *body, int body_len,
                               char *buf, uint32_t max)
{
    char *p = buf;
    char *end = buf + max - 1;

    #define APPEND(s) do { \
        const char *_s = (s); \
        while (*_s && p < end) *p++ = *_s++; \
    } while(0)

    /* Request line */
    APPEND(method);
    APPEND(" ");
    APPEND(path);
    APPEND(" HTTP/1.1\r\n");

    /* Host header */
    APPEND("Host: ");
    APPEND(host_str);
    APPEND("\r\n");

    /* Common headers */
    APPEND("User-Agent: AlJefra-OS/1.0\r\n");
    APPEND("Connection: close\r\n");

    if (body && body_len > 0) {
        APPEND("Content-Type: application/json\r\n");
        APPEND("Content-Length: ");
        char lenbuf[12];
        int_to_str(body_len, lenbuf);
        APPEND(lenbuf);
        APPEND("\r\n");
    }

    APPEND("\r\n");

    /* Body */
    if (body && body_len > 0) {
        for (int i = 0; i < body_len && p < end; i++)
            *p++ = body[i];
    }

    *p = '\0';

    #undef APPEND
    return (int)(p - buf);
}

/* ── Parse HTTP response: extract status code ── */
static int parse_http_status(const char *response, uint32_t len)
{
    /* "HTTP/1.x NNN ..." */
    if (len < 12) return -1;
    if (response[0] != 'H' || response[1] != 'T' ||
        response[2] != 'T' || response[3] != 'P')
        return -1;

    /* Find first space after "HTTP/1.x" */
    uint32_t i = 0;
    while (i < len && response[i] != ' ') i++;
    i++; /* skip space */
    if (i + 3 > len) return -1;

    /* Parse 3-digit status code */
    int status = 0;
    for (int j = 0; j < 3 && i + j < len; j++) {
        char c = response[i + j];
        if (c < '0' || c > '9') return -1;
        status = status * 10 + (c - '0');
    }
    return status;
}

/* ── Find HTTP body start (after \r\n\r\n) ── */
static const char *find_http_body(const char *response, uint32_t len)
{
    for (uint32_t i = 0; i + 3 < len; i++) {
        if (response[i] == '\r' && response[i+1] == '\n' &&
            response[i+2] == '\r' && response[i+3] == '\n')
            return &response[i + 4];
    }
    return NULL;
}

/* ── Public API ── */

void marketplace_set_gateway(uint32_t gateway_ip)
{
    g_gateway_ip = gateway_ip;
}

hal_status_t marketplace_connect(void)
{
    uint32_t server_ip;

#if MARKETPLACE_USE_LOCAL
    /* Use gateway IP as the marketplace server (QEMU host) */
    server_ip = g_gateway_ip;
    if (server_ip == 0) {
        /* Default: QEMU user-mode networking gateway */
        server_ip = 0x0A000202; /* 10.0.2.2 */
    }
#else
    /* TODO: DNS resolve api.aljefra.com */
    server_ip = 0x0A000202; /* placeholder */
#endif

    hal_console_printf("[marketplace] Connecting to %u.%u.%u.%u:%u...\n",
                       (server_ip >> 24) & 0xFF, (server_ip >> 16) & 0xFF,
                       (server_ip >> 8) & 0xFF, server_ip & 0xFF,
                       MARKETPLACE_PORT);

    hal_status_t rc = tcp_connect(&g_tcp_conn, server_ip, MARKETPLACE_PORT);
    if (rc != HAL_OK) {
        hal_console_puts("[marketplace] TCP connection failed\n");
        g_connected = 0;
        return rc;
    }

    g_connected = 1;
    hal_console_puts("[marketplace] Connected (HTTP)\n");
    return HAL_OK;
}

void marketplace_disconnect(void)
{
    if (g_connected) {
        tcp_close(&g_tcp_conn);
        g_connected = 0;
        hal_console_puts("[marketplace] Disconnected\n");
    }
}

hal_status_t marketplace_send_manifest(const hardware_manifest_t *manifest)
{
    if (!g_connected)
        return HAL_ERROR;

    /* Build JSON payload */
    char json[4096];
    int json_len = build_json_manifest(manifest, json, sizeof(json));

    hal_console_printf("[marketplace] Manifest JSON: %d bytes\n", json_len);

    /* Build Host header value */
    char host_str[32];
    ip_to_str(g_tcp_conn.remote_ip, host_str);
    char *p = host_str;
    while (*p) p++;
    *p++ = ':';
    int_to_str(MARKETPLACE_PORT, p);

    /* Build HTTP POST request */
    char request[8192];
    int req_len = build_http_request("POST", API_MANIFEST, host_str,
                                      json, json_len, request, sizeof(request));

    hal_console_printf("[marketplace] Sending %d byte HTTP POST...\n", req_len);

    /* Send request */
    int32_t sent = tcp_send(&g_tcp_conn, request, (uint32_t)req_len);
    if (sent < 0) {
        hal_console_puts("[marketplace] TCP send failed\n");
        return HAL_ERROR;
    }

    /* Receive response */
    char response[8192];
    int32_t resp_len = tcp_recv(&g_tcp_conn, response, sizeof(response) - 1, 10000);
    if (resp_len <= 0) {
        hal_console_puts("[marketplace] No response received\n");
        return HAL_TIMEOUT;
    }
    response[resp_len] = '\0';

    /* Parse HTTP status */
    int status = parse_http_status(response, (uint32_t)resp_len);
    hal_console_printf("[marketplace] HTTP %d, %d bytes\n", status, resp_len);

    /* Find and print body */
    const char *body = find_http_body(response, (uint32_t)resp_len);
    if (body) {
        uint32_t body_len = (uint32_t)(resp_len - (body - response));
        /* Print first 200 chars of response body */
        hal_console_puts("[marketplace] Response: ");
        for (uint32_t i = 0; i < body_len && i < 200; i++)
            hal_console_putc(body[i]);
        hal_console_putc('\n');
    }

    if (status != 200) {
        hal_console_printf("[marketplace] Unexpected status %d\n", status);
        return HAL_ERROR;
    }

    return HAL_OK;
}

hal_status_t marketplace_get_driver(uint16_t vendor_id, uint16_t device_id,
                                     void **data, uint64_t *size)
{
    *data = NULL;
    *size = 0;

    /* Build URL: /v1/drivers/<vendor>/<device>/<arch> */
    char path[256];
    char *p = path;
    const char *base = API_DRIVER "/";
    while (*base) *p++ = *base++;

    char hex[5];
    u16_to_hex(vendor_id, hex);
    for (int i = 0; i < 4; i++) *p++ = hex[i];
    *p++ = '/';
    u16_to_hex(device_id, hex);
    for (int i = 0; i < 4; i++) *p++ = hex[i];
    *p++ = '/';

    switch (hal_arch()) {
    case HAL_ARCH_X86_64:
        { const char *s = "x86_64"; while (*s) *p++ = *s++; }
        break;
    case HAL_ARCH_AARCH64:
        { const char *s = "aarch64"; while (*s) *p++ = *s++; }
        break;
    case HAL_ARCH_RISCV64:
        { const char *s = "riscv64"; while (*s) *p++ = *s++; }
        break;
    }
    *p = '\0';

    hal_console_printf("[marketplace] GET %s\n", path);

    /* Need a fresh TCP connection for each request (Connection: close) */
    tcp_conn_t drv_conn;
    hal_status_t rc = tcp_connect(&drv_conn, g_tcp_conn.remote_ip, MARKETPLACE_PORT);
    if (rc != HAL_OK) {
        hal_console_puts("[marketplace] TCP connect failed for driver download\n");
        return HAL_ERROR;
    }

    /* Build HTTP GET request */
    char host_str[32];
    ip_to_str(drv_conn.remote_ip, host_str);
    char *hp = host_str;
    while (*hp) hp++;
    *hp++ = ':';
    int_to_str(MARKETPLACE_PORT, hp);

    char request[2048];
    int req_len = build_http_request("GET", path, host_str, NULL, 0,
                                      request, sizeof(request));

    int32_t sent = tcp_send(&drv_conn, request, (uint32_t)req_len);
    if (sent < 0) {
        tcp_close(&drv_conn);
        return HAL_ERROR;
    }

    /* Receive response */
    char response[8192];
    int32_t resp_len = tcp_recv(&drv_conn, response, sizeof(response) - 1, 10000);
    tcp_close(&drv_conn);

    if (resp_len <= 0)
        return HAL_TIMEOUT;

    response[resp_len] = '\0';

    /* Check status */
    int status = parse_http_status(response, (uint32_t)resp_len);
    hal_console_printf("[marketplace] Driver download: HTTP %d\n", status);

    if (status == 404) {
        hal_console_printf("[marketplace] No driver for %04x:%04x\n", vendor_id, device_id);
        return HAL_NO_DEVICE;
    }

    if (status != 200)
        return HAL_ERROR;

    /* Find body (the .ajdrv binary) */
    const char *body = find_http_body(response, (uint32_t)resp_len);
    if (!body)
        return HAL_ERROR;

    uint32_t body_len = (uint32_t)(resp_len - (body - response));
    if (body_len == 0)
        return HAL_ERROR;

    /* Allocate and copy the binary */
    uint64_t phys_addr;
    void *drv_buf = hal_dma_alloc(body_len, &phys_addr);
    if (!drv_buf)
        return HAL_ERROR;

    memcpy(drv_buf, body, body_len);
    *data = drv_buf;
    *size = body_len;

    hal_console_printf("[marketplace] Downloaded %u bytes for %04x:%04x\n",
                       body_len, vendor_id, device_id);

    return HAL_OK;
}

/* ── Minimal JSON field extraction helpers ── */

/* Find a JSON string value: "key":"value" → copies value into out, returns length */
static int json_find_string(const char *json, uint32_t json_len,
                             const char *key, char *out, uint32_t out_max)
{
    uint32_t key_len = str_len(key);
    for (uint32_t i = 0; i + key_len + 4 < json_len; i++) {
        if (json[i] != '"') continue;
        /* Match key */
        int match = 1;
        for (uint32_t k = 0; k < key_len; k++) {
            if (json[i + 1 + k] != key[k]) { match = 0; break; }
        }
        if (!match || json[i + 1 + key_len] != '"') continue;
        /* Skip ": */
        uint32_t j = i + 1 + key_len + 1;
        while (j < json_len && (json[j] == ':' || json[j] == ' ')) j++;
        if (j >= json_len || json[j] != '"') continue;
        j++; /* skip opening quote */
        /* Copy value */
        uint32_t n = 0;
        while (j + n < json_len && json[j + n] != '"' && n < out_max - 1)
            n++;
        memcpy(out, &json[j], n);
        out[n] = '\0';
        return (int)n;
    }
    out[0] = '\0';
    return 0;
}

/* Find a JSON boolean: "key":true/false → returns 1/0, -1 if not found */
static int json_find_bool(const char *json, uint32_t json_len, const char *key)
{
    uint32_t key_len = str_len(key);
    for (uint32_t i = 0; i + key_len + 4 < json_len; i++) {
        if (json[i] != '"') continue;
        int match = 1;
        for (uint32_t k = 0; k < key_len; k++) {
            if (json[i + 1 + k] != key[k]) { match = 0; break; }
        }
        if (!match || json[i + 1 + key_len] != '"') continue;
        uint32_t j = i + 1 + key_len + 1;
        while (j < json_len && (json[j] == ':' || json[j] == ' ')) j++;
        if (j < json_len && json[j] == 't') return 1;
        if (j < json_len && json[j] == 'f') return 0;
    }
    return -1;
}

/* ── Shared helper: perform HTTP GET on fresh TCP connection ── */
static int http_get(const char *path, char *response, uint32_t resp_max)
{
    tcp_conn_t conn;
    hal_status_t rc = tcp_connect(&conn, g_tcp_conn.remote_ip, MARKETPLACE_PORT);
    if (rc != HAL_OK)
        return -1;

    char host_str[32];
    ip_to_str(conn.remote_ip, host_str);
    char *hp = host_str;
    while (*hp) hp++;
    *hp++ = ':';
    int_to_str(MARKETPLACE_PORT, hp);

    char request[2048];
    int req_len = build_http_request("GET", path, host_str, NULL, 0,
                                      request, sizeof(request));

    int32_t sent = tcp_send(&conn, request, (uint32_t)req_len);
    if (sent < 0) {
        tcp_close(&conn);
        return -1;
    }

    int32_t resp_len = tcp_recv(&conn, response, resp_max - 1, 10000);
    tcp_close(&conn);

    if (resp_len <= 0)
        return -1;

    response[resp_len] = '\0';
    return (int)resp_len;
}

hal_status_t marketplace_check_updates(const char *os_version,
                                        char *update_url, uint32_t url_max)
{
    if (!g_connected)
        return HAL_ERROR;

    /* Build path: /v1/updates/<os_version> */
    char path[128];
    char *p = path;
    const char *base = API_UPDATES "/";
    while (*base) *p++ = *base++;
    while (*os_version) *p++ = *os_version++;
    *p = '\0';

    hal_console_printf("[marketplace] GET %s\n", path);

    char response[4096];
    int resp_len = http_get(path, response, sizeof(response));
    if (resp_len < 0)
        return HAL_TIMEOUT;

    int status = parse_http_status(response, (uint32_t)resp_len);
    hal_console_printf("[marketplace] Update check: HTTP %d\n", status);

    if (status != 200)
        return HAL_ERROR;

    const char *body = find_http_body(response, (uint32_t)resp_len);
    if (!body)
        return HAL_ERROR;

    uint32_t body_len = (uint32_t)(resp_len - (body - response));

    /* Parse JSON response: {"update_available":bool,"version":"...","download_url":"..."} */
    int update_available = json_find_bool(body, body_len, "update_available");

    if (update_available <= 0) {
        hal_console_puts("[marketplace] No updates available\n");
        if (update_url && url_max > 0) update_url[0] = '\0';
        return HAL_OK;
    }

    /* Extract update URL */
    char version[32];
    json_find_string(body, body_len, "version", version, sizeof(version));
    json_find_string(body, body_len, "download_url", update_url, url_max);

    hal_console_printf("[marketplace] Update available: v%s at %s\n", version, update_url);
    return HAL_OK;
}

hal_status_t marketplace_get_catalog(marketplace_driver_info_t *drivers,
                                      uint32_t max, uint32_t *count)
{
    *count = 0;

    if (!g_connected)
        return HAL_ERROR;

    /* Build path with arch filter: /v1/catalog?arch=<arch> */
    char path[128];
    char *p = path;
    const char *base = API_CATALOG "?arch=";
    while (*base) *p++ = *base++;

    switch (hal_arch()) {
    case HAL_ARCH_X86_64:  { const char *s = "x86_64";  while (*s) *p++ = *s++; } break;
    case HAL_ARCH_AARCH64: { const char *s = "aarch64"; while (*s) *p++ = *s++; } break;
    case HAL_ARCH_RISCV64: { const char *s = "riscv64"; while (*s) *p++ = *s++; } break;
    }
    *p = '\0';

    hal_console_printf("[marketplace] GET %s\n", path);

    char response[8192];
    int resp_len = http_get(path, response, sizeof(response));
    if (resp_len < 0)
        return HAL_TIMEOUT;

    int status = parse_http_status(response, (uint32_t)resp_len);
    hal_console_printf("[marketplace] Catalog: HTTP %d\n", status);

    if (status != 200)
        return HAL_ERROR;

    const char *body = find_http_body(response, (uint32_t)resp_len);
    if (!body)
        return HAL_ERROR;

    uint32_t body_len = (uint32_t)(resp_len - (body - response));

    /* Parse JSON array of drivers.
     * Server returns: {"drivers":[{...},{...}],"total":N}
     * Each entry has: name, vendor_id, device_id, version, arch, etc.
     * We scan for each object within the "drivers" array. */
    const char *scan = body;
    const char *body_end = body + body_len;
    uint32_t n = 0;

    /* Find start of drivers array */
    while (scan < body_end - 1) {
        if (*scan == '[') { scan++; break; }
        scan++;
    }

    /* Parse each object {...} in the array */
    while (scan < body_end && n < max) {
        /* Find next '{' */
        while (scan < body_end && *scan != '{') {
            if (*scan == ']') goto done;  /* end of array */
            scan++;
        }
        if (scan >= body_end) break;

        /* Find matching '}' (no nesting expected) */
        const char *obj_start = scan;
        int depth = 1;
        scan++;
        while (scan < body_end && depth > 0) {
            if (*scan == '{') depth++;
            else if (*scan == '}') depth--;
            scan++;
        }
        uint32_t obj_len = (uint32_t)(scan - obj_start);

        /* Extract fields from this driver object */
        marketplace_driver_info_t *drv = &drivers[n];
        memset(drv, 0, sizeof(*drv));

        json_find_string(obj_start, obj_len, "name", drv->driver_name, sizeof(drv->driver_name));
        json_find_string(obj_start, obj_len, "version", drv->version, sizeof(drv->version));
        json_find_string(obj_start, obj_len, "sha256", drv->sha256, sizeof(drv->sha256));
        json_find_string(obj_start, obj_len, "download_url", drv->download_url, sizeof(drv->download_url));

        /* Parse vendor_id and device_id from hex strings */
        char hex_buf[8];
        if (json_find_string(obj_start, obj_len, "vendor_id", hex_buf, sizeof(hex_buf)) > 0) {
            uint16_t v = 0;
            for (int i = 0; hex_buf[i]; i++) {
                v <<= 4;
                char c = hex_buf[i];
                if (c >= '0' && c <= '9') v |= (uint16_t)(c - '0');
                else if (c >= 'a' && c <= 'f') v |= (uint16_t)(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') v |= (uint16_t)(c - 'A' + 10);
            }
            drv->vendor_id = v;
        }
        if (json_find_string(obj_start, obj_len, "device_id", hex_buf, sizeof(hex_buf)) > 0) {
            uint16_t v = 0;
            for (int i = 0; hex_buf[i]; i++) {
                v <<= 4;
                char c = hex_buf[i];
                if (c >= '0' && c <= '9') v |= (uint16_t)(c - '0');
                else if (c >= 'a' && c <= 'f') v |= (uint16_t)(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') v |= (uint16_t)(c - 'A' + 10);
            }
            drv->device_id = v;
        }

        /* Only count entries with a valid name */
        if (drv->driver_name[0]) n++;
    }

done:
    *count = n;
    hal_console_printf("[marketplace] Catalog: %u drivers\n", n);
    return HAL_OK;
}

/* ── Shared helper: HTTP POST with JSON body on fresh TCP connection ── */
static int http_post(const char *path, const char *json_body, int body_len,
                     char *response, uint32_t resp_max)
{
    tcp_conn_t conn;
    hal_status_t rc = tcp_connect(&conn, g_tcp_conn.remote_ip, MARKETPLACE_PORT);
    if (rc != HAL_OK)
        return -1;

    char host_str[32];
    ip_to_str(conn.remote_ip, host_str);
    char *hp = host_str;
    while (*hp) hp++;
    *hp++ = ':';
    int_to_str(MARKETPLACE_PORT, hp);

    char request[4096];
    int req_len = build_http_request("POST", path, host_str,
                                      json_body, body_len, request, sizeof(request));

    int32_t sent = tcp_send(&conn, request, (uint32_t)req_len);
    if (sent < 0) { tcp_close(&conn); return -1; }

    int32_t resp_len = tcp_recv(&conn, response, resp_max - 1, 10000);
    tcp_close(&conn);

    if (resp_len <= 0) return -1;
    response[resp_len] = '\0';
    return (int)resp_len;
}

/* ── Report driver metrics for quality tracking ── */
hal_status_t marketplace_report_metrics(const char *driver_name,
                                         const char *version,
                                         uint32_t uptime_secs,
                                         uint32_t error_count)
{
    if (!g_connected)
        return HAL_ERROR;

    /* Build JSON payload */
    char json[512];
    char *p = json;
    char *end = json + sizeof(json) - 1;

    #define JAPPEND(s) do { const char *_s = (s); while (*_s && p < end) *p++ = *_s++; } while(0)

    JAPPEND("{\"driver_name\":\"");
    JAPPEND(driver_name);
    JAPPEND("\",\"version\":\"");
    JAPPEND(version);
    JAPPEND("\",\"arch\":\"");
    switch (hal_arch()) {
    case HAL_ARCH_X86_64:  JAPPEND("x86_64"); break;
    case HAL_ARCH_AARCH64: JAPPEND("aarch64"); break;
    case HAL_ARCH_RISCV64: JAPPEND("riscv64"); break;
    }
    JAPPEND("\",\"uptime_secs\":");
    char num[16];
    int_to_str(uptime_secs, num);
    JAPPEND(num);
    JAPPEND(",\"error_count\":");
    int_to_str(error_count, num);
    JAPPEND(num);
    JAPPEND("}");
    *p = '\0';
    #undef JAPPEND

    int json_len = (int)(p - json);

    char response[1024];
    int resp_len = http_post("/v1/metrics", json, json_len, response, sizeof(response));
    if (resp_len < 0)
        return HAL_ERROR;

    int status = parse_http_status(response, (uint32_t)resp_len);
    return (status == 200 || status == 201) ? HAL_OK : HAL_ERROR;
}

/* ── Submit evolved driver variant ── */
hal_status_t marketplace_submit_evolution(const char *base_driver,
                                           const char *description,
                                           const void *ajdrv_data,
                                           uint64_t ajdrv_size)
{
    if (!g_connected)
        return HAL_ERROR;

    /* Build JSON metadata (binary upload not supported over raw TCP/HTTP,
     * so we send metadata only — the binary can be uploaded via the web API) */
    char json[1024];
    char *p = json;
    char *end = json + sizeof(json) - 1;

    #define JAPPEND(s) do { const char *_s = (s); while (*_s && p < end) *p++ = *_s++; } while(0)

    JAPPEND("{\"base_driver\":\"");
    JAPPEND(base_driver);
    JAPPEND("\",\"arch\":\"");
    switch (hal_arch()) {
    case HAL_ARCH_X86_64:  JAPPEND("x86_64"); break;
    case HAL_ARCH_AARCH64: JAPPEND("aarch64"); break;
    case HAL_ARCH_RISCV64: JAPPEND("riscv64"); break;
    }
    JAPPEND("\",\"optimization_type\":\"performance\"");
    JAPPEND(",\"description\":\"");
    JAPPEND(description);
    JAPPEND("\",\"author\":\"aljefra-os-agent\"");
    JAPPEND(",\"has_binary\":true");

    char num[16];
    JAPPEND(",\"code_size\":");
    int_to_str((uint32_t)ajdrv_size, num);
    JAPPEND(num);
    JAPPEND("}");
    *p = '\0';
    #undef JAPPEND

    int json_len = (int)(p - json);
    (void)ajdrv_data; /* Binary upload via multipart is future work */

    char response[1024];
    int resp_len = http_post("/v1/evolve", json, json_len, response, sizeof(response));
    if (resp_len < 0)
        return HAL_ERROR;

    int status = parse_http_status(response, (uint32_t)resp_len);
    if (status == 200 || status == 201) {
        hal_console_puts("[marketplace] Evolution submitted successfully\n");
        return HAL_OK;
    }
    return HAL_ERROR;
}
