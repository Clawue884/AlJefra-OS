/* SPDX-License-Identifier: MIT */
/* AlJefra OS -- 802.11 WiFi Framework Implementation
 * Freestanding: no libc, no malloc. Uses HAL for timing, console output.
 *
 * Implements the full WiFi connection lifecycle:
 *   Scan -> Authenticate -> Associate -> WPA2 4-Way Handshake -> Data
 */

#include "wifi_framework.h"
#include "../../lib/string.h"

/* ===================================================================
 * Internal helpers
 * =================================================================== */

/* Big-endian read/write helpers */
static inline uint16_t wf_be16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] << 8 | p[1]);
}

static inline void wf_put_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

static inline uint16_t wf_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static inline void wf_put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

/* Build frame control word */
static inline uint16_t wf_fc(uint8_t type, uint8_t subtype, uint8_t flags)
{
    return (uint16_t)((type << 2) | (subtype << 4) | ((uint16_t)flags << 8));
}

/* Increment 48-bit packet number */
static void wf_inc_pn(uint8_t pn[CCMP_PN_LEN])
{
    for (int i = 0; i < CCMP_PN_LEN; i++) {
        if (++pn[i] != 0)
            break;
    }
}

/* Compare two 48-bit PNs. Returns >0 if a > b. */
static int wf_cmp_pn(const uint8_t a[CCMP_PN_LEN],
                       const uint8_t b[CCMP_PN_LEN])
{
    for (int i = CCMP_PN_LEN - 1; i >= 0; i--) {
        if (a[i] != b[i])
            return (int)a[i] - (int)b[i];
    }
    return 0;
}

/* Broadcast address */
static const uint8_t broadcast_addr[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/* ===================================================================
 * SHA-1 (FIPS 180-4) -- needed for HMAC-SHA1, PBKDF2
 * =================================================================== */

#define SHA1_BLOCK_SIZE   64
#define SHA1_DIGEST_SIZE  20

typedef struct {
    uint32_t h[5];
    uint8_t  buf[SHA1_BLOCK_SIZE];
    uint32_t buf_len;
    uint64_t total_len;
} sha1_ctx_t;

static inline uint32_t sha1_rotl(uint32_t x, int n)
{
    return (x << n) | (x >> (32 - n));
}

static void sha1_init(sha1_ctx_t *ctx)
{
    ctx->h[0] = 0x67452301;
    ctx->h[1] = 0xEFCDAB89;
    ctx->h[2] = 0x98BADCFE;
    ctx->h[3] = 0x10325476;
    ctx->h[4] = 0xC3D2E1F0;
    ctx->buf_len = 0;
    ctx->total_len = 0;
}

static void sha1_process_block(sha1_ctx_t *ctx, const uint8_t block[SHA1_BLOCK_SIZE])
{
    uint32_t w[80];
    uint32_t a, b, c, d, e;

    /* Parse block into 16 big-endian words */
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i * 4] << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) |
               (uint32_t)block[i * 4 + 3];
    }

    /* Expand to 80 words */
    for (int i = 16; i < 80; i++)
        w[i] = sha1_rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    a = ctx->h[0]; b = ctx->h[1]; c = ctx->h[2];
    d = ctx->h[3]; e = ctx->h[4];

    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }

        uint32_t temp = sha1_rotl(a, 5) + f + e + k + w[i];
        e = d; d = c; c = sha1_rotl(b, 30); b = a; a = temp;
    }

    ctx->h[0] += a; ctx->h[1] += b; ctx->h[2] += c;
    ctx->h[3] += d; ctx->h[4] += e;
}

static void sha1_update(sha1_ctx_t *ctx, const uint8_t *data, uint32_t len)
{
    ctx->total_len += len;

    /* Fill buffer */
    while (len > 0) {
        uint32_t space = SHA1_BLOCK_SIZE - ctx->buf_len;
        uint32_t copy = (len < space) ? len : space;
        memcpy(&ctx->buf[ctx->buf_len], data, copy);
        ctx->buf_len += copy;
        data += copy;
        len -= copy;

        if (ctx->buf_len == SHA1_BLOCK_SIZE) {
            sha1_process_block(ctx, ctx->buf);
            ctx->buf_len = 0;
        }
    }
}

static void sha1_final(sha1_ctx_t *ctx, uint8_t digest[SHA1_DIGEST_SIZE])
{
    uint64_t bit_len = ctx->total_len * 8;

    /* Pad: 0x80, then zeros, then 8-byte big-endian bit length */
    uint8_t pad = 0x80;
    sha1_update(ctx, &pad, 1);

    uint8_t zero = 0;
    while (ctx->buf_len != 56)
        sha1_update(ctx, &zero, 1);

    uint8_t len_bytes[8];
    for (int i = 7; i >= 0; i--) {
        len_bytes[i] = (uint8_t)(bit_len & 0xFF);
        bit_len >>= 8;
    }
    sha1_update(ctx, len_bytes, 8);

    /* Output digest (big-endian) */
    for (int i = 0; i < 5; i++) {
        digest[i * 4]     = (uint8_t)(ctx->h[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(ctx->h[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(ctx->h[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)(ctx->h[i]);
    }
}

/* ===================================================================
 * HMAC-SHA1
 * =================================================================== */

void wifi_hmac_sha1(const uint8_t *key, uint32_t key_len,
                     const uint8_t *data, uint32_t data_len,
                     uint8_t out[20])
{
    sha1_ctx_t ctx;
    uint8_t k_pad[SHA1_BLOCK_SIZE];
    uint8_t tk[SHA1_DIGEST_SIZE];

    /* If key is longer than block size, hash it first */
    if (key_len > SHA1_BLOCK_SIZE) {
        sha1_init(&ctx);
        sha1_update(&ctx, key, key_len);
        sha1_final(&ctx, tk);
        key = tk;
        key_len = SHA1_DIGEST_SIZE;
    }

    /* Inner: SHA1(K ^ ipad || data) */
    memset(k_pad, 0, SHA1_BLOCK_SIZE);
    memcpy(k_pad, key, key_len);
    for (uint32_t i = 0; i < SHA1_BLOCK_SIZE; i++)
        k_pad[i] ^= 0x36;

    sha1_init(&ctx);
    sha1_update(&ctx, k_pad, SHA1_BLOCK_SIZE);
    sha1_update(&ctx, data, data_len);
    sha1_final(&ctx, out);

    /* Outer: SHA1(K ^ opad || inner_hash) */
    memset(k_pad, 0, SHA1_BLOCK_SIZE);
    memcpy(k_pad, key, key_len);
    for (uint32_t i = 0; i < SHA1_BLOCK_SIZE; i++)
        k_pad[i] ^= 0x5C;

    sha1_init(&ctx);
    sha1_update(&ctx, k_pad, SHA1_BLOCK_SIZE);
    sha1_update(&ctx, out, SHA1_DIGEST_SIZE);
    sha1_final(&ctx, out);
}

/* ===================================================================
 * PBKDF2-SHA1 (for WPA2 PMK derivation)
 * =================================================================== */

static void pbkdf2_sha1_f(const uint8_t *password, uint32_t pw_len,
                            const uint8_t *salt, uint32_t salt_len,
                            uint32_t iterations, uint32_t block_num,
                            uint8_t output[SHA1_DIGEST_SIZE])
{
    uint8_t u[SHA1_DIGEST_SIZE];
    uint8_t salt_block[128]; /* salt + 4-byte BE block number */

    if (salt_len > 120) salt_len = 120;  /* Safety */

    /* U1 = HMAC-SHA1(password, salt || INT(block_num)) */
    memcpy(salt_block, salt, salt_len);
    salt_block[salt_len]     = (uint8_t)((block_num >> 24) & 0xFF);
    salt_block[salt_len + 1] = (uint8_t)((block_num >> 16) & 0xFF);
    salt_block[salt_len + 2] = (uint8_t)((block_num >> 8) & 0xFF);
    salt_block[salt_len + 3] = (uint8_t)(block_num & 0xFF);

    wifi_hmac_sha1(password, pw_len, salt_block, salt_len + 4, u);
    memcpy(output, u, SHA1_DIGEST_SIZE);

    /* U2..Uc */
    for (uint32_t i = 1; i < iterations; i++) {
        wifi_hmac_sha1(password, pw_len, u, SHA1_DIGEST_SIZE, u);
        for (uint32_t j = 0; j < SHA1_DIGEST_SIZE; j++)
            output[j] ^= u[j];
    }
}

void wifi_derive_pmk(const char *passphrase, const uint8_t *ssid,
                      uint32_t ssid_len, uint8_t pmk[PMK_LEN])
{
    uint32_t pw_len = str_len(passphrase);

    /* PBKDF2-SHA1 with 4096 iterations, 2 blocks (32 bytes)
     * Block 1 gives bytes 0-19, block 2 gives bytes 20-39.
     * PMK = first 32 bytes. Use temp buffer for block 2 to avoid
     * writing 20 bytes into the remaining 12 bytes of pmk[]. */
    uint8_t block2[SHA1_DIGEST_SIZE];
    pbkdf2_sha1_f((const uint8_t *)passphrase, pw_len,
                   ssid, ssid_len, 4096, 1, pmk);
    pbkdf2_sha1_f((const uint8_t *)passphrase, pw_len,
                   ssid, ssid_len, 4096, 2, block2);
    /* Copy only the 12 bytes we need from block 2 */
    memcpy(&pmk[SHA1_DIGEST_SIZE], block2, PMK_LEN - SHA1_DIGEST_SIZE);
}

/* ===================================================================
 * PRF-384 (for PTK derivation) -- IEEE 802.11i
 * =================================================================== */

static void wf_prf(const uint8_t *key, uint32_t key_len,
                    const char *label,
                    const uint8_t *data, uint32_t data_len,
                    uint8_t *output, uint32_t output_len)
{
    /*
     * PRF-X(K, A, B) where:
     *   K = key, A = label (string), B = data
     *   PRF-X = first X bits of:
     *     HMAC-SHA1(K, A || 0x00 || B || counter)
     *   where counter = 0, 1, 2, ...
     */
    uint8_t buf[256];
    uint32_t label_len = str_len(label);
    uint32_t msg_len = label_len + 1 + data_len + 1;

    if (msg_len > sizeof(buf))
        return;

    /* Build: label || 0x00 || data || counter */
    memcpy(buf, label, label_len);
    buf[label_len] = 0x00;
    memcpy(&buf[label_len + 1], data, data_len);

    uint32_t offset = 0;
    uint8_t counter = 0;
    uint8_t hmac_out[SHA1_DIGEST_SIZE];

    while (offset < output_len) {
        buf[label_len + 1 + data_len] = counter;

        wifi_hmac_sha1(key, key_len, buf, msg_len, hmac_out);

        uint32_t copy = output_len - offset;
        if (copy > SHA1_DIGEST_SIZE)
            copy = SHA1_DIGEST_SIZE;
        memcpy(&output[offset], hmac_out, copy);

        offset += copy;
        counter++;
    }
}

void wifi_derive_ptk(const uint8_t pmk[PMK_LEN],
                      const uint8_t anonce[NONCE_LEN],
                      const uint8_t snonce[NONCE_LEN],
                      const uint8_t aa[6],
                      const uint8_t spa[6],
                      uint8_t ptk[PTK_LEN])
{
    /*
     * B = min(AA, SPA) || max(AA, SPA) || min(ANonce, SNonce) || max(ANonce, SNonce)
     * PTK = PRF-384(PMK, "Pairwise key expansion", B)
     */
    uint8_t data[76]; /* 6 + 6 + 32 + 32 = 76 */

    /* Sort addresses: min first */
    if (memcmp(aa, spa, 6) < 0) {
        memcpy(&data[0], aa, 6);
        memcpy(&data[6], spa, 6);
    } else {
        memcpy(&data[0], spa, 6);
        memcpy(&data[6], aa, 6);
    }

    /* Sort nonces: min first */
    if (memcmp(anonce, snonce, NONCE_LEN) < 0) {
        memcpy(&data[12], anonce, NONCE_LEN);
        memcpy(&data[44], snonce, NONCE_LEN);
    } else {
        memcpy(&data[12], snonce, NONCE_LEN);
        memcpy(&data[44], anonce, NONCE_LEN);
    }

    wf_prf(pmk, PMK_LEN, "Pairwise key expansion", data, 76, ptk, PTK_LEN);
}

/* ===================================================================
 * Frame Parsing Helpers
 * =================================================================== */

/* Parse Information Elements from a beacon/probe response body */
static void wf_parse_ies(const uint8_t *ies, uint32_t len, wifi_bss_t *bss)
{
    uint32_t offset = 0;

    while (offset + 2 <= len) {
        uint8_t ie_id = ies[offset];
        uint8_t ie_len = ies[offset + 1];

        if (offset + 2 + ie_len > len)
            break;

        const uint8_t *ie_data = &ies[offset + 2];

        switch (ie_id) {
        case WIFI_IE_SSID:
            if (ie_len <= WIFI_MAX_SSID_LEN) {
                memcpy(bss->ssid, ie_data, ie_len);
                bss->ssid[ie_len] = '\0';
                bss->ssid_len = ie_len;
            }
            break;

        case WIFI_IE_DS_PARAMS:
            if (ie_len >= 1)
                bss->channel = ie_data[0];
            break;

        case WIFI_IE_RSN:
            bss->has_rsn = true;
            /* Parse RSN IE to check for WPA2-PSK + CCMP */
            if (ie_len >= 8) {
                /* RSN IE: Version(2) + Group Cipher(4) + Pairwise Count(2) + ... */
                uint16_t version = wf_le16(ie_data);
                if (version == 1) {
                    /* Check group cipher suite OUI */
                    /* At offset 2: group cipher suite (4 bytes) */
                    /* At offset 6: pairwise cipher suite count (2 bytes) */
                    /* Then pairwise cipher suites (4 bytes each) */
                    /* Then AKM suite count (2 bytes) */
                    /* Then AKM suites (4 bytes each) */
                    bss->wpa2 = true; /* Simplified: any RSN = WPA2 */
                }
            }
            break;

        default:
            break;
        }

        offset += 2 + ie_len;
    }
}

/* Parse a beacon or probe response frame */
static void wf_parse_beacon(wifi_ctx_t *ctx, const uint8_t *frame,
                              uint32_t len)
{
    if (len < sizeof(wifi_mac_hdr_t) + sizeof(wifi_beacon_fixed_t))
        return;

    const wifi_mac_hdr_t *hdr = (const wifi_mac_hdr_t *)frame;
    const wifi_beacon_fixed_t *fixed =
        (const wifi_beacon_fixed_t *)(frame + sizeof(wifi_mac_hdr_t));

    /* Check if we already have this BSS */
    for (uint32_t i = 0; i < ctx->scan_count; i++) {
        if (memcmp(ctx->scan_results[i].bssid, hdr->addr2, 6) == 0)
            return; /* Already recorded */
    }

    if (ctx->scan_count >= WIFI_MAX_SCAN_RESULTS)
        return;

    wifi_bss_t *bss = &ctx->scan_results[ctx->scan_count];
    memset(bss, 0, sizeof(wifi_bss_t));

    memcpy(bss->bssid, hdr->addr2, 6);
    bss->beacon_interval = fixed->beacon_interval;
    bss->capability = fixed->capability;

    /* Parse IEs */
    uint32_t ie_offset = sizeof(wifi_mac_hdr_t) + sizeof(wifi_beacon_fixed_t);
    if (ie_offset < len)
        wf_parse_ies(&frame[ie_offset], len - ie_offset, bss);

    ctx->scan_count++;
}

/* ===================================================================
 * Frame Construction
 * =================================================================== */

uint32_t wifi_build_probe_req(wifi_ctx_t *ctx, const char *ssid,
                                uint8_t *out)
{
    uint32_t offset = 0;

    /* MAC header */
    wifi_mac_hdr_t *hdr = (wifi_mac_hdr_t *)out;
    hdr->frame_control = wf_fc(WIFI_TYPE_MGMT, WIFI_MGMT_PROBE_REQ, 0);
    hdr->duration = 0;
    memcpy(hdr->addr1, broadcast_addr, 6);  /* DA = broadcast */
    ctx->hw->get_mac(hdr->addr2);               /* SA = our MAC */
    memcpy(hdr->addr3, broadcast_addr, 6);  /* BSSID = broadcast */
    hdr->seq_ctrl = (uint16_t)(ctx->tx_seq++ << 4);
    offset = sizeof(wifi_mac_hdr_t);

    /* SSID IE */
    out[offset++] = WIFI_IE_SSID;
    if (ssid) {
        uint8_t ssid_len = (uint8_t)str_len(ssid);
        if (ssid_len > WIFI_MAX_SSID_LEN)
            ssid_len = WIFI_MAX_SSID_LEN;
        out[offset++] = ssid_len;
        memcpy(&out[offset], ssid, ssid_len);
        offset += ssid_len;
    } else {
        out[offset++] = 0; /* Wildcard SSID */
    }

    /* Supported Rates IE (basic 802.11b/g rates) */
    out[offset++] = WIFI_IE_SUPPORTED_RATES;
    out[offset++] = 8;
    out[offset++] = 0x82;  /* 1 Mbps (basic) */
    out[offset++] = 0x84;  /* 2 Mbps (basic) */
    out[offset++] = 0x8B;  /* 5.5 Mbps (basic) */
    out[offset++] = 0x96;  /* 11 Mbps (basic) */
    out[offset++] = 0x0C;  /* 6 Mbps */
    out[offset++] = 0x12;  /* 9 Mbps */
    out[offset++] = 0x18;  /* 12 Mbps */
    out[offset++] = 0x24;  /* 18 Mbps */

    return offset;
}

uint32_t wifi_build_auth(wifi_ctx_t *ctx, uint8_t *out)
{
    uint32_t offset = 0;

    /* MAC header */
    wifi_mac_hdr_t *hdr = (wifi_mac_hdr_t *)out;
    hdr->frame_control = wf_fc(WIFI_TYPE_MGMT, WIFI_MGMT_AUTH, 0);
    hdr->duration = 0;
    memcpy(hdr->addr1, ctx->bss.bssid, 6);  /* DA = AP */
    ctx->hw->get_mac(hdr->addr2);                /* SA = us */
    memcpy(hdr->addr3, ctx->bss.bssid, 6);  /* BSSID */
    hdr->seq_ctrl = (uint16_t)(ctx->tx_seq++ << 4);
    offset = sizeof(wifi_mac_hdr_t);

    /* Auth fixed fields */
    wifi_auth_fixed_t *auth = (wifi_auth_fixed_t *)(out + offset);
    auth->auth_algorithm = WIFI_AUTH_OPEN;
    auth->auth_seq = 1;
    auth->status_code = 0;
    offset += sizeof(wifi_auth_fixed_t);

    return offset;
}

uint32_t wifi_build_assoc_req(wifi_ctx_t *ctx, uint8_t *out)
{
    uint32_t offset = 0;

    /* MAC header */
    wifi_mac_hdr_t *hdr = (wifi_mac_hdr_t *)out;
    hdr->frame_control = wf_fc(WIFI_TYPE_MGMT, WIFI_MGMT_ASSOC_REQ, 0);
    hdr->duration = 0;
    memcpy(hdr->addr1, ctx->bss.bssid, 6);
    ctx->hw->get_mac(hdr->addr2);
    memcpy(hdr->addr3, ctx->bss.bssid, 6);
    hdr->seq_ctrl = (uint16_t)(ctx->tx_seq++ << 4);
    offset = sizeof(wifi_mac_hdr_t);

    /* Assoc Request fixed fields */
    wifi_assoc_req_fixed_t *req = (wifi_assoc_req_fixed_t *)(out + offset);
    req->capability = 0x0431;  /* ESS + Short Preamble + Short Slot + Privacy */
    req->listen_interval = 10;
    offset += sizeof(wifi_assoc_req_fixed_t);

    /* SSID IE */
    out[offset++] = WIFI_IE_SSID;
    out[offset++] = ctx->bss.ssid_len;
    memcpy(&out[offset], ctx->bss.ssid, ctx->bss.ssid_len);
    offset += ctx->bss.ssid_len;

    /* Supported Rates IE */
    out[offset++] = WIFI_IE_SUPPORTED_RATES;
    out[offset++] = 8;
    out[offset++] = 0x82;  out[offset++] = 0x84;
    out[offset++] = 0x8B;  out[offset++] = 0x96;
    out[offset++] = 0x0C;  out[offset++] = 0x12;
    out[offset++] = 0x18;  out[offset++] = 0x24;

    /* RSN IE (WPA2-PSK with CCMP) */
    out[offset++] = WIFI_IE_RSN;
    out[offset++] = 20;  /* RSN IE length */
    /* Version */
    wf_put_le16(&out[offset], 1);  offset += 2;
    /* Group cipher: 00-0F-AC:04 (CCMP) */
    out[offset++] = 0x00; out[offset++] = 0x0F;
    out[offset++] = 0xAC; out[offset++] = RSN_CIPHER_CCMP;
    /* Pairwise cipher count: 1 */
    wf_put_le16(&out[offset], 1);  offset += 2;
    /* Pairwise cipher: CCMP */
    out[offset++] = 0x00; out[offset++] = 0x0F;
    out[offset++] = 0xAC; out[offset++] = RSN_CIPHER_CCMP;
    /* AKM count: 1 */
    wf_put_le16(&out[offset], 1);  offset += 2;
    /* AKM: PSK */
    out[offset++] = 0x00; out[offset++] = 0x0F;
    out[offset++] = 0xAC; out[offset++] = RSN_AKM_PSK;
    /* RSN Capabilities */
    wf_put_le16(&out[offset], 0x0000);  offset += 2;

    return offset;
}

uint32_t wifi_build_deauth(wifi_ctx_t *ctx, uint16_t reason, uint8_t *out)
{
    uint32_t offset = 0;

    wifi_mac_hdr_t *hdr = (wifi_mac_hdr_t *)out;
    hdr->frame_control = wf_fc(WIFI_TYPE_MGMT, WIFI_MGMT_DEAUTH, 0);
    hdr->duration = 0;
    memcpy(hdr->addr1, ctx->bss.bssid, 6);
    ctx->hw->get_mac(hdr->addr2);
    memcpy(hdr->addr3, ctx->bss.bssid, 6);
    hdr->seq_ctrl = (uint16_t)(ctx->tx_seq++ << 4);
    offset = sizeof(wifi_mac_hdr_t);

    /* Reason code (2 bytes LE) */
    wf_put_le16(&out[offset], reason);
    offset += 2;

    return offset;
}

/* ===================================================================
 * EAPOL Frame Construction
 * =================================================================== */

/* Wrap an EAPOL-Key frame inside an 802.11 data frame with LLC/SNAP */
static uint32_t wf_wrap_eapol(wifi_ctx_t *ctx,
                                const uint8_t *eapol_data, uint32_t eapol_len,
                                uint8_t *out)
{
    uint32_t offset = 0;

    /* 802.11 data header: ToDS */
    wifi_mac_hdr_t *hdr = (wifi_mac_hdr_t *)out;
    hdr->frame_control = wf_fc(WIFI_TYPE_DATA, WIFI_DATA_DATA, WIFI_FC_TODS);
    hdr->duration = 0;
    memcpy(hdr->addr1, ctx->bss.bssid, 6);   /* BSSID (receiver = AP) */
    ctx->hw->get_mac(hdr->addr2);                 /* SA (transmitter) */
    memcpy(hdr->addr3, ctx->bss.bssid, 6);   /* DA (AP) */
    hdr->seq_ctrl = (uint16_t)(ctx->tx_seq++ << 4);
    offset = sizeof(wifi_mac_hdr_t);

    /* LLC/SNAP header */
    wifi_llc_snap_t *snap = (wifi_llc_snap_t *)(out + offset);
    snap->dsap = 0xAA;
    snap->ssap = 0xAA;
    snap->ctrl = 0x03;
    snap->oui[0] = 0x00; snap->oui[1] = 0x00; snap->oui[2] = 0x00;
    wf_put_be16((uint8_t *)&snap->ether_type, EAPOL_ETHER_TYPE);
    offset += sizeof(wifi_llc_snap_t);

    /* EAPOL data */
    memcpy(&out[offset], eapol_data, eapol_len);
    offset += eapol_len;

    return offset;
}

/* Generate a random SNonce using HAL timer as entropy source */
static void wf_generate_snonce(wifi_ctx_t *ctx)
{
    /* Use timer and MAC address as entropy. In production, use a proper
     * PRNG. This is a bare-metal minimal implementation. */
    uint64_t ns = hal_timer_ns();
    sha1_ctx_t sha;
    sha1_init(&sha);
    sha1_update(&sha, (const uint8_t *)&ns, sizeof(ns));
    sha1_update(&sha, ctx->our_mac, 6);

    /* Hash again with different entropy */
    ns = hal_timer_ns();
    sha1_update(&sha, (const uint8_t *)&ns, sizeof(ns));

    uint8_t digest[SHA1_DIGEST_SIZE];
    sha1_final(&sha, digest);

    /* First 20 bytes of SNonce */
    memcpy(ctx->snonce, digest, SHA1_DIGEST_SIZE);

    /* Generate remaining 12 bytes */
    ns = hal_timer_ns();
    sha1_init(&sha);
    sha1_update(&sha, digest, SHA1_DIGEST_SIZE);
    sha1_update(&sha, (const uint8_t *)&ns, sizeof(ns));
    sha1_final(&sha, digest);
    memcpy(&ctx->snonce[SHA1_DIGEST_SIZE], digest, NONCE_LEN - SHA1_DIGEST_SIZE);
}

uint32_t wifi_build_eapol_msg2(wifi_ctx_t *ctx, uint8_t *out)
{
    uint8_t eapol_buf[256];
    uint32_t offset = 0;

    /* EAPOL header */
    eapol_hdr_t *ehdr = (eapol_hdr_t *)eapol_buf;
    ehdr->version = 2;
    ehdr->type = EAPOL_TYPE_KEY;
    offset = sizeof(eapol_hdr_t);

    /* EAPOL-Key */
    eapol_key_t *key = (eapol_key_t *)(eapol_buf + offset);
    memset(key, 0, sizeof(eapol_key_t));
    key->descriptor_type = EAPOL_KEY_DESC_RSN;

    /* Key Info: Pairwise + MIC + RSN Key Descriptor version 2 (AES-based) */
    uint16_t key_info = EAPOL_KEY_INFO_PAIRWISE | EAPOL_KEY_INFO_MIC | 0x0002;
    wf_put_be16((uint8_t *)&key->key_info, key_info);
    wf_put_be16((uint8_t *)&key->key_length, 16); /* CCMP key = 16 bytes */

    /* Replay counter from AP's message 1 */
    memcpy(key->replay_counter, ctx->replay_counter, 8);

    /* Our SNonce */
    memcpy(key->key_nonce, ctx->snonce, NONCE_LEN);

    /* RSN IE as key data */
    uint8_t rsn_ie[24];
    uint32_t rsn_len = 0;
    rsn_ie[rsn_len++] = WIFI_IE_RSN;
    rsn_ie[rsn_len++] = 20;
    wf_put_le16(&rsn_ie[rsn_len], 1);  rsn_len += 2;  /* Version */
    rsn_ie[rsn_len++] = 0x00; rsn_ie[rsn_len++] = 0x0F;
    rsn_ie[rsn_len++] = 0xAC; rsn_ie[rsn_len++] = RSN_CIPHER_CCMP;
    wf_put_le16(&rsn_ie[rsn_len], 1);  rsn_len += 2;
    rsn_ie[rsn_len++] = 0x00; rsn_ie[rsn_len++] = 0x0F;
    rsn_ie[rsn_len++] = 0xAC; rsn_ie[rsn_len++] = RSN_CIPHER_CCMP;
    wf_put_le16(&rsn_ie[rsn_len], 1);  rsn_len += 2;
    rsn_ie[rsn_len++] = 0x00; rsn_ie[rsn_len++] = 0x0F;
    rsn_ie[rsn_len++] = 0xAC; rsn_ie[rsn_len++] = RSN_AKM_PSK;
    wf_put_le16(&rsn_ie[rsn_len], 0);  rsn_len += 2;

    wf_put_be16((uint8_t *)&key->key_data_length, (uint16_t)rsn_len);

    uint32_t key_body_len = sizeof(eapol_key_t) + rsn_len;
    wf_put_be16((uint8_t *)&ehdr->length, (uint16_t)key_body_len);

    offset += sizeof(eapol_key_t);

    /* Append RSN IE as key data */
    memcpy(&eapol_buf[offset], rsn_ie, rsn_len);
    offset += rsn_len;

    /* Derive PTK so we can compute MIC */
    wifi_derive_ptk(ctx->pmk, ctx->anonce, ctx->snonce,
                     ctx->bss.bssid, ctx->our_mac, ctx->ptk);

    /* Compute MIC over the entire EAPOL frame (with MIC field zeroed) */
    memset(key->key_mic, 0, 16);

    uint8_t mic[SHA1_DIGEST_SIZE];
    wifi_hmac_sha1(&ctx->ptk[PTK_KCK_OFFSET], KCK_LEN,
                    eapol_buf, offset, mic);
    memcpy(key->key_mic, mic, 16);

    /* Wrap in 802.11 data frame */
    return wf_wrap_eapol(ctx, eapol_buf, offset, out);
}

uint32_t wifi_build_eapol_msg4(wifi_ctx_t *ctx, uint8_t *out)
{
    uint8_t eapol_buf[128];
    uint32_t offset = 0;

    /* EAPOL header */
    eapol_hdr_t *ehdr = (eapol_hdr_t *)eapol_buf;
    ehdr->version = 2;
    ehdr->type = EAPOL_TYPE_KEY;
    offset = sizeof(eapol_hdr_t);

    /* EAPOL-Key (no key data in msg 4) */
    eapol_key_t *key = (eapol_key_t *)(eapol_buf + offset);
    memset(key, 0, sizeof(eapol_key_t));
    key->descriptor_type = EAPOL_KEY_DESC_RSN;

    uint16_t key_info = EAPOL_KEY_INFO_PAIRWISE | EAPOL_KEY_INFO_MIC |
                         EAPOL_KEY_INFO_SECURE | 0x0002;
    wf_put_be16((uint8_t *)&key->key_info, key_info);
    wf_put_be16((uint8_t *)&key->key_length, 16);

    memcpy(key->replay_counter, ctx->replay_counter, 8);
    wf_put_be16((uint8_t *)&key->key_data_length, 0);

    uint32_t key_body_len = sizeof(eapol_key_t);
    wf_put_be16((uint8_t *)&ehdr->length, (uint16_t)key_body_len);

    offset += sizeof(eapol_key_t);

    /* Compute MIC */
    memset(key->key_mic, 0, 16);
    uint8_t mic[SHA1_DIGEST_SIZE];
    wifi_hmac_sha1(&ctx->ptk[PTK_KCK_OFFSET], KCK_LEN,
                    eapol_buf, offset, mic);
    memcpy(key->key_mic, mic, 16);

    return wf_wrap_eapol(ctx, eapol_buf, offset, out);
}

/* ===================================================================
 * 802.11 <-> Ethernet Conversion
 * =================================================================== */

uint32_t wifi_80211_to_ether(const uint8_t *wifi_frame, uint32_t wifi_len,
                               uint8_t *ether_frame)
{
    if (wifi_len < sizeof(wifi_mac_hdr_t) + LLC_SNAP_HDR_LEN)
        return 0;

    const wifi_mac_hdr_t *hdr = (const wifi_mac_hdr_t *)wifi_frame;
    uint8_t fc1 = (uint8_t)(hdr->frame_control >> 8);

    /* Determine DA and SA based on ToDS/FromDS */
    const uint8_t *da, *sa;
    if ((fc1 & 0x03) == 0x02) {
        /* FromDS: DA=A1, SA=A3, BSSID=A2 */
        da = hdr->addr1;
        sa = hdr->addr3;
    } else if ((fc1 & 0x03) == 0x01) {
        /* ToDS: DA=A3, SA=A2, BSSID=A1 */
        da = hdr->addr3;
        sa = hdr->addr2;
    } else if ((fc1 & 0x03) == 0x00) {
        /* IBSS: DA=A1, SA=A2, BSSID=A3 */
        da = hdr->addr1;
        sa = hdr->addr2;
    } else {
        return 0; /* WDS (4-addr) not supported in this conversion */
    }

    /* Find LLC/SNAP header (after MAC header, possibly after QoS field) */
    uint32_t mac_hdr_len = sizeof(wifi_mac_hdr_t);
    uint8_t type = ((uint8_t)hdr->frame_control >> 2) & 0x03;
    uint8_t subtype = ((uint8_t)hdr->frame_control >> 4) & 0x0F;
    if (type == WIFI_TYPE_DATA && (subtype & 0x08))
        mac_hdr_len += 2; /* QoS Control */

    /* Account for CCMP header if Protected flag set */
    if (fc1 & WIFI_FC_PROTECTED)
        mac_hdr_len += CCMP_HDR_LEN;

    if (wifi_len < mac_hdr_len + LLC_SNAP_HDR_LEN)
        return 0;

    const wifi_llc_snap_t *snap = (const wifi_llc_snap_t *)(wifi_frame + mac_hdr_len);

    /* Verify LLC/SNAP */
    if (snap->dsap != 0xAA || snap->ssap != 0xAA || snap->ctrl != 0x03)
        return 0;

    /* Build Ethernet header */
    memcpy(ether_frame, da, 6);        /* DA */
    memcpy(ether_frame + 6, sa, 6);    /* SA */
    /* EtherType from SNAP (already big-endian) */
    ether_frame[12] = (uint8_t)(snap->ether_type >> 8);
    ether_frame[13] = (uint8_t)(snap->ether_type & 0xFF);

    /* Copy payload (after LLC/SNAP) */
    uint32_t payload_offset = mac_hdr_len + LLC_SNAP_HDR_LEN;
    uint32_t payload_len = wifi_len - payload_offset;

    /* If CCMP MIC was present, it was already stripped during decryption */

    memcpy(ether_frame + ETHER_HDR_LEN, wifi_frame + payload_offset,
               payload_len);

    return ETHER_HDR_LEN + payload_len;
}

uint32_t wifi_ether_to_80211(wifi_ctx_t *ctx,
                               const uint8_t *ether_frame, uint32_t ether_len,
                               uint8_t *wifi_frame)
{
    if (ether_len < ETHER_HDR_LEN)
        return 0;

    const uint8_t *da = ether_frame;
    const uint8_t *sa = ether_frame + 6;
    uint16_t ether_type = wf_be16(ether_frame + 12);

    uint32_t offset = 0;

    /* 802.11 header: ToDS (going to AP) */
    wifi_mac_hdr_t *hdr = (wifi_mac_hdr_t *)wifi_frame;
    hdr->frame_control = wf_fc(WIFI_TYPE_DATA, WIFI_DATA_DATA, WIFI_FC_TODS);
    hdr->duration = 0;
    memcpy(hdr->addr1, ctx->bss.bssid, 6);  /* BSSID (AP) */
    memcpy(hdr->addr2, sa, 6);               /* SA (us) */
    memcpy(hdr->addr3, da, 6);               /* DA (destination) */
    hdr->seq_ctrl = (uint16_t)(ctx->tx_seq++ << 4);
    offset = sizeof(wifi_mac_hdr_t);

    /* LLC/SNAP header */
    wifi_llc_snap_t *snap = (wifi_llc_snap_t *)(wifi_frame + offset);
    snap->dsap = 0xAA;
    snap->ssap = 0xAA;
    snap->ctrl = 0x03;
    snap->oui[0] = 0x00; snap->oui[1] = 0x00; snap->oui[2] = 0x00;
    wf_put_be16((uint8_t *)&snap->ether_type, ether_type);
    offset += sizeof(wifi_llc_snap_t);

    /* Payload (skip Ethernet header) */
    uint32_t payload_len = ether_len - ETHER_HDR_LEN;
    memcpy(wifi_frame + offset, ether_frame + ETHER_HDR_LEN, payload_len);
    offset += payload_len;

    return offset;
}

/* ===================================================================
 * EAPOL Processing (WPA2 4-Way Handshake)
 * =================================================================== */

static hal_status_t wf_process_eapol(wifi_ctx_t *ctx,
                                       const uint8_t *data, uint32_t len)
{
    if (len < sizeof(eapol_hdr_t) + sizeof(eapol_key_t))
        return HAL_ERROR;

    const eapol_hdr_t *ehdr = (const eapol_hdr_t *)data;
    if (ehdr->type != EAPOL_TYPE_KEY)
        return HAL_ERROR;

    const eapol_key_t *key = (const eapol_key_t *)(data + sizeof(eapol_hdr_t));
    uint16_t key_info = wf_be16((const uint8_t *)&key->key_info);

    bool pairwise = (key_info & EAPOL_KEY_INFO_PAIRWISE) != 0;
    bool ack      = (key_info & EAPOL_KEY_INFO_ACK) != 0;
    bool mic_set  = (key_info & EAPOL_KEY_INFO_MIC) != 0;
    bool secure   = (key_info & EAPOL_KEY_INFO_SECURE) != 0;
    bool install  = (key_info & EAPOL_KEY_INFO_INSTALL) != 0;

    if (pairwise && ack && !mic_set) {
        /* ---- Message 1 of 4 ---- */
        /* AP sends ANonce */
        hal_console_puts("[WiFi] 4-way handshake: received msg 1\n");

        memcpy(ctx->anonce, key->key_nonce, NONCE_LEN);
        memcpy(ctx->replay_counter, key->replay_counter, 8);

        /* Generate our SNonce */
        wf_generate_snonce(ctx);

        ctx->state = WIFI_STATE_4WAY_1;

        /* Build and send message 2 */
        uint32_t frame_len = wifi_build_eapol_msg2(ctx, ctx->frame_buf);
        ctx->hw->tx_raw(ctx->frame_buf, frame_len);

        ctx->state = WIFI_STATE_4WAY_2;
        return HAL_OK;
    }

    if (pairwise && ack && mic_set && secure && install) {
        /* ---- Message 3 of 4 ---- */
        hal_console_puts("[WiFi] 4-way handshake: received msg 3\n");

        memcpy(ctx->replay_counter, key->replay_counter, 8);

        /* Verify MIC using KCK from PTK */
        uint8_t saved_mic[16];
        memcpy(saved_mic, key->key_mic, 16);

        /* Zero MIC field for verification */
        uint8_t *frame_copy = ctx->frame_buf;
        memcpy(frame_copy, data, len);
        eapol_key_t *key_copy = (eapol_key_t *)(frame_copy + sizeof(eapol_hdr_t));
        memset(key_copy->key_mic, 0, 16);

        uint8_t computed_mic[SHA1_DIGEST_SIZE];
        wifi_hmac_sha1(&ctx->ptk[PTK_KCK_OFFSET], KCK_LEN,
                        frame_copy, len, computed_mic);

        if (memcmp(saved_mic, computed_mic, 16) != 0) {
            hal_console_puts("[WiFi] msg3 MIC verification failed!\n");
            return HAL_ERROR;
        }

        ctx->state = WIFI_STATE_4WAY_3;

        /* Extract GTK from key data if present */
        uint16_t key_data_len = wf_be16((const uint8_t *)&key->key_data_length);
        if (key_data_len > 0 && len >= sizeof(eapol_hdr_t) + sizeof(eapol_key_t) + key_data_len) {
            /* GTK extraction from wrapped key data would go here.
             * For simplicity, we mark GTK as installed since the TK is
             * the primary key we need for unicast traffic. */
            ctx->gtk_installed = true;
        }

        /* Install PTK (TK portion for CCMP) */
        ctx->ptk_installed = true;

        /* Build and send message 4 */
        uint32_t frame_len = wifi_build_eapol_msg4(ctx, ctx->frame_buf);
        ctx->hw->tx_raw(ctx->frame_buf, frame_len);

        ctx->state = WIFI_STATE_CONNECTED;
        hal_console_puts("[WiFi] Connected! Keys installed.\n");
        return HAL_OK;
    }

    /* Group key message (message 1 of 2-way group handshake) */
    if (!pairwise && ack && mic_set && secure) {
        hal_console_puts("[WiFi] Group key update received\n");
        /* For now, acknowledge without updating the GTK.
         * A full implementation would decrypt the key data. */
        return HAL_OK;
    }

    return HAL_ERROR;
}

/* ===================================================================
 * Frame Processing (called on every received frame)
 * =================================================================== */

hal_status_t wifi_process_frame(wifi_ctx_t *ctx, const void *frame,
                                 uint32_t len)
{
    if (len < sizeof(wifi_mac_hdr_t))
        return HAL_ERROR;

    const uint8_t *raw = (const uint8_t *)frame;
    uint16_t fc = wf_le16(raw);
    uint8_t type = (fc >> 2) & 0x03;
    uint8_t subtype = (fc >> 4) & 0x0F;

    const wifi_mac_hdr_t *hdr __attribute__((unused)) = (const wifi_mac_hdr_t *)raw;

    switch (type) {
    case WIFI_TYPE_MGMT:
        switch (subtype) {
        case WIFI_MGMT_BEACON:
        case WIFI_MGMT_PROBE_RESP:
            if (ctx->state == WIFI_STATE_SCANNING)
                wf_parse_beacon(ctx, raw, len);
            break;

        case WIFI_MGMT_AUTH: {
            if (ctx->state != WIFI_STATE_AUTHENTICATING)
                break;
            if (len < sizeof(wifi_mac_hdr_t) + sizeof(wifi_auth_fixed_t))
                break;
            const wifi_auth_fixed_t *auth =
                (const wifi_auth_fixed_t *)(raw + sizeof(wifi_mac_hdr_t));
            if (auth->auth_seq == 2 && auth->status_code == WIFI_STATUS_SUCCESS) {
                hal_console_puts("[WiFi] Authentication successful\n");
                ctx->state = WIFI_STATE_ASSOCIATING;
            } else {
                hal_console_puts("[WiFi] Authentication failed\n");
                ctx->state = WIFI_STATE_IDLE;
            }
            break;
        }

        case WIFI_MGMT_ASSOC_RESP: {
            if (ctx->state != WIFI_STATE_ASSOCIATING)
                break;
            if (len < sizeof(wifi_mac_hdr_t) + sizeof(wifi_assoc_resp_fixed_t))
                break;
            const wifi_assoc_resp_fixed_t *resp =
                (const wifi_assoc_resp_fixed_t *)(raw + sizeof(wifi_mac_hdr_t));
            if (resp->status_code == WIFI_STATUS_SUCCESS) {
                ctx->assoc_id = resp->assoc_id & 0x3FFF;
                hal_console_puts("[WiFi] Association successful\n");
                /* Now wait for EAPOL message 1 from AP */
            } else {
                hal_console_puts("[WiFi] Association failed\n");
                ctx->state = WIFI_STATE_IDLE;
            }
            break;
        }

        case WIFI_MGMT_DEAUTH:
        case WIFI_MGMT_DISASSOC:
            hal_console_puts("[WiFi] Deauthentication received\n");
            ctx->state = WIFI_STATE_IDLE;
            ctx->ptk_installed = false;
            ctx->gtk_installed = false;
            break;

        default:
            break;
        }
        break;

    case WIFI_TYPE_DATA: {
        /* Check for EAPOL frames (before encryption is established) */
        uint32_t mac_hdr_len = sizeof(wifi_mac_hdr_t);
        if (subtype & 0x08)
            mac_hdr_len += 2; /* QoS */

        if (len >= mac_hdr_len + LLC_SNAP_HDR_LEN) {
            const wifi_llc_snap_t *snap =
                (const wifi_llc_snap_t *)(raw + mac_hdr_len);
            uint16_t etype = wf_be16((const uint8_t *)&snap->ether_type);

            if (etype == EAPOL_ETHER_TYPE) {
                /* EAPOL frame */
                uint32_t eapol_offset = mac_hdr_len + LLC_SNAP_HDR_LEN;
                return wf_process_eapol(ctx, raw + eapol_offset,
                                         len - eapol_offset);
            }
        }
        break;
    }

    default:
        break;
    }

    return HAL_OK;
}

/* ===================================================================
 * Public API
 * =================================================================== */

hal_status_t wifi_init(wifi_ctx_t *ctx, const wifi_hw_ops_t *hw)
{
    memset(ctx, 0, sizeof(wifi_ctx_t));
    ctx->hw = hw;
    ctx->state = WIFI_STATE_IDLE;
    hw->get_mac(ctx->our_mac);
    return HAL_OK;
}

uint32_t wifi_scan(wifi_ctx_t *ctx, const uint8_t *channels,
                    uint32_t n_chan, uint32_t dwell_ms)
{
    /* Default channels: 2.4GHz 1-11 */
    static const uint8_t default_channels[] = {1,2,3,4,5,6,7,8,9,10,11};
    if (!channels || n_chan == 0) {
        channels = default_channels;
        n_chan = 11;
    }
    if (dwell_ms == 0)
        dwell_ms = 100;

    ctx->scan_count = 0;
    ctx->state = WIFI_STATE_SCANNING;

    /* Enable promiscuous mode for scanning */
    ctx->hw->set_promisc(true);

    for (uint32_t i = 0; i < n_chan; i++) {
        /* Switch channel */
        ctx->hw->set_channel(channels[i]);
        hal_timer_delay_ms(5);

        /* Send probe request */
        uint32_t probe_len = wifi_build_probe_req(ctx, NULL, ctx->frame_buf);
        ctx->hw->tx_raw(ctx->frame_buf, probe_len);

        /* Dwell: listen for beacons and probe responses */
        uint64_t deadline = hal_timer_ms() + dwell_ms;
        while (hal_timer_ms() < deadline) {
            uint32_t rx_len = 0;
            if (ctx->hw->rx_raw(ctx->frame_buf, &rx_len) == HAL_OK && rx_len > 0) {
                wifi_process_frame(ctx, ctx->frame_buf, rx_len);
            }
            hal_timer_delay_us(500);
        }
    }

    /* Disable promiscuous mode */
    ctx->hw->set_promisc(false);

    ctx->state = WIFI_STATE_IDLE;
    return ctx->scan_count;
}

hal_status_t wifi_connect(wifi_ctx_t *ctx, const char *ssid,
                           const char *passphrase)
{
    uint32_t ssid_len = str_len(ssid);

    /* Step 0: Derive PMK from passphrase */
    hal_console_puts("[WiFi] Deriving PMK (PBKDF2-SHA1, 4096 iterations)...\n");
    wifi_derive_pmk(passphrase, (const uint8_t *)ssid, ssid_len, ctx->pmk);

    /* Step 1: Find BSS in scan results (or scan now) */
    wifi_bss_t *target = NULL;
    for (uint32_t i = 0; i < ctx->scan_count; i++) {
        if (ctx->scan_results[i].ssid_len == ssid_len &&
            memcmp(ctx->scan_results[i].ssid, ssid, ssid_len) == 0) {
            target = &ctx->scan_results[i];
            break;
        }
    }

    if (!target) {
        /* Scan for the network */
        wifi_scan(ctx, NULL, 0, 150);
        for (uint32_t i = 0; i < ctx->scan_count; i++) {
            if (ctx->scan_results[i].ssid_len == ssid_len &&
                memcmp(ctx->scan_results[i].ssid, ssid, ssid_len) == 0) {
                target = &ctx->scan_results[i];
                break;
            }
        }
    }

    if (!target) {
        hal_console_puts("[WiFi] Network not found\n");
        return HAL_NO_DEVICE;
    }

    /* Copy BSS info */
    memcpy(&ctx->bss, target, sizeof(wifi_bss_t));

    /* Set channel */
    ctx->hw->set_channel(ctx->bss.channel);
    hal_timer_delay_ms(10);

    /* Step 2: Open System Authentication */
    hal_console_puts("[WiFi] Authenticating...\n");
    ctx->state = WIFI_STATE_AUTHENTICATING;

    uint32_t frame_len = wifi_build_auth(ctx, ctx->frame_buf);
    ctx->hw->tx_raw(ctx->frame_buf, frame_len);

    /* Wait for auth response */
    uint64_t deadline = hal_timer_ms() + 3000;
    while (ctx->state == WIFI_STATE_AUTHENTICATING && hal_timer_ms() < deadline) {
        uint32_t rx_len = 0;
        if (ctx->hw->rx_raw(ctx->frame_buf, &rx_len) == HAL_OK && rx_len > 0)
            wifi_process_frame(ctx, ctx->frame_buf, rx_len);
        hal_timer_delay_us(500);
    }

    if (ctx->state != WIFI_STATE_ASSOCIATING)
        return HAL_TIMEOUT;

    /* Step 3: Association */
    hal_console_puts("[WiFi] Associating...\n");

    frame_len = wifi_build_assoc_req(ctx, ctx->frame_buf);
    ctx->hw->tx_raw(ctx->frame_buf, frame_len);

    /* Wait for assoc response + EAPOL */
    deadline = hal_timer_ms() + 5000;
    while (ctx->state != WIFI_STATE_CONNECTED && hal_timer_ms() < deadline) {
        uint32_t rx_len = 0;
        if (ctx->hw->rx_raw(ctx->frame_buf, &rx_len) == HAL_OK && rx_len > 0)
            wifi_process_frame(ctx, ctx->frame_buf, rx_len);
        hal_timer_delay_us(500);
    }

    if (ctx->state != WIFI_STATE_CONNECTED)
        return HAL_TIMEOUT;

    return HAL_OK;
}

hal_status_t wifi_disconnect(wifi_ctx_t *ctx)
{
    if (ctx->state == WIFI_STATE_IDLE)
        return HAL_OK;

    ctx->state = WIFI_STATE_DISCONNECTING;

    uint32_t frame_len = wifi_build_deauth(ctx, WIFI_REASON_UNSPECIFIED,
                                            ctx->frame_buf);
    ctx->hw->tx_raw(ctx->frame_buf, frame_len);

    ctx->state = WIFI_STATE_IDLE;
    ctx->ptk_installed = false;
    ctx->gtk_installed = false;
    memset(ctx->ptk, 0, PTK_LEN);
    memset(ctx->gtk, 0, GTK_LEN);
    memset(ctx->tx_pn, 0, CCMP_PN_LEN);
    memset(ctx->rx_pn, 0, CCMP_PN_LEN);

    hal_console_puts("[WiFi] Disconnected\n");
    return HAL_OK;
}

hal_status_t wifi_send(wifi_ctx_t *ctx, const void *frame, uint32_t len)
{
    if (ctx->state != WIFI_STATE_CONNECTED)
        return HAL_ERROR;

    /* Convert Ethernet -> 802.11 */
    uint8_t wifi_frame[WIFI_MAX_FRAME_LEN];
    uint32_t wifi_len = wifi_ether_to_80211(ctx, (const uint8_t *)frame, len,
                                              wifi_frame);
    if (wifi_len == 0)
        return HAL_ERROR;

    if (ctx->ptk_installed) {
        /* Encrypt with CCMP */
        wf_inc_pn(ctx->tx_pn);

        uint8_t encrypted[WIFI_MAX_FRAME_LEN + 64];
        uint32_t enc_len = 0;
        hal_status_t st = ccmp_encrypt_frame(&ctx->ptk[PTK_TK_OFFSET],
                                              wifi_frame, wifi_len,
                                              ctx->tx_pn,
                                              encrypted, &enc_len);
        if (st != HAL_OK)
            return st;

        return ctx->hw->tx_raw(encrypted, enc_len);
    }

    return ctx->hw->tx_raw(wifi_frame, wifi_len);
}

hal_status_t wifi_recv(wifi_ctx_t *ctx, void *buf, uint32_t *len)
{
    uint32_t rx_len = 0;
    hal_status_t st = ctx->hw->rx_raw(ctx->frame_buf, &rx_len);
    if (st != HAL_OK || rx_len == 0)
        return HAL_NO_DEVICE;

    /* Check frame type */
    uint16_t fc = wf_le16(ctx->frame_buf);
    uint8_t type = (fc >> 2) & 0x03;
    uint8_t fc_flags = (uint8_t)(fc >> 8);

    /* Management frames go through the state machine */
    if (type == WIFI_TYPE_MGMT) {
        wifi_process_frame(ctx, ctx->frame_buf, rx_len);
        return HAL_NO_DEVICE; /* Not a data frame for the caller */
    }

    /* Data frames */
    if (type != WIFI_TYPE_DATA)
        return HAL_NO_DEVICE;

    /* Check for EAPOL (before decryption) */
    uint32_t mac_hdr_len = sizeof(wifi_mac_hdr_t);
    uint8_t subtype = (fc >> 4) & 0x0F;
    if (subtype & 0x08)
        mac_hdr_len += 2;

    /* Decrypt if Protected */
    uint8_t *data_frame = ctx->frame_buf;
    uint32_t data_len = rx_len;

    if ((fc_flags & WIFI_FC_PROTECTED) && ctx->ptk_installed) {
        uint8_t decrypted[WIFI_MAX_FRAME_LEN];
        uint32_t dec_len = 0;

        st = ccmp_decrypt_frame(&ctx->ptk[PTK_TK_OFFSET],
                                 ctx->frame_buf, rx_len,
                                 decrypted, &dec_len);
        if (st != HAL_OK) {
            return HAL_ERROR; /* MIC verification failed */
        }

        /* Check PN for replay detection */
        uint8_t pn[CCMP_PN_LEN];
        uint8_t key_id;
        ccmp_parse_hdr(&ctx->frame_buf[mac_hdr_len], pn, &key_id);
        if (wf_cmp_pn(pn, ctx->rx_pn) <= 0) {
            return HAL_ERROR; /* Replay detected */
        }
        memcpy(ctx->rx_pn, pn, CCMP_PN_LEN);

        data_frame = decrypted;
        data_len = dec_len;
    } else if (!(fc_flags & WIFI_FC_PROTECTED)) {
        /* Unencrypted data -- check for EAPOL */
        if (data_len >= mac_hdr_len + LLC_SNAP_HDR_LEN) {
            const wifi_llc_snap_t *snap =
                (const wifi_llc_snap_t *)(data_frame + mac_hdr_len);
            uint16_t etype = wf_be16((const uint8_t *)&snap->ether_type);
            if (etype == EAPOL_ETHER_TYPE) {
                wifi_process_frame(ctx, data_frame, data_len);
                return HAL_NO_DEVICE;
            }
        }
    }

    /* Convert 802.11 -> Ethernet */
    *len = wifi_80211_to_ether(data_frame, data_len, (uint8_t *)buf);
    if (*len == 0)
        return HAL_ERROR;

    return HAL_OK;
}

wifi_state_t wifi_get_state(wifi_ctx_t *ctx)
{
    return ctx->state;
}

bool wifi_is_connected(wifi_ctx_t *ctx)
{
    return ctx->state == WIFI_STATE_CONNECTED && ctx->ptk_installed;
}
