/* SPDX-License-Identifier: MIT */
/* AlJefra OS -- AES-128 CCMP Implementation
 * Freestanding AES-128 + CCM mode for WPA2 802.11i.
 * No libc dependencies -- all tables are compile-time constants.
 */

#include "aes_ccmp.h"
#include "../../lib/string.h"

/* ===================================================================
 * Internal helpers
 * =================================================================== */

static void ccmp_xor_block(uint8_t *dst, const uint8_t *a, const uint8_t *b,
                            uint32_t len)
{
    for (uint32_t i = 0; i < len; i++)
        dst[i] = a[i] ^ b[i];
}

/* ===================================================================
 * AES-128 S-Box and lookup tables
 * =================================================================== */

static const uint8_t aes_sbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5,
    0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0,
    0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc,
    0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a,
    0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0,
    0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b,
    0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85,
    0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5,
    0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17,
    0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88,
    0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c,
    0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9,
    0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6,
    0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e,
    0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94,
    0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68,
    0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
};

/* Round constants for key expansion */
static const uint8_t aes_rcon[10] = {
    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36,
};

/* ===================================================================
 * AES-128 Key Expansion
 * =================================================================== */

static inline uint32_t aes_word(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return ((uint32_t)a << 24) | ((uint32_t)b << 16) |
           ((uint32_t)c << 8) | (uint32_t)d;
}

static inline uint8_t aes_byte(uint32_t w, int n)
{
    return (uint8_t)(w >> (24 - 8 * n));
}

static uint32_t aes_sub_word(uint32_t w)
{
    return aes_word(aes_sbox[aes_byte(w, 0)], aes_sbox[aes_byte(w, 1)],
                    aes_sbox[aes_byte(w, 2)], aes_sbox[aes_byte(w, 3)]);
}

static uint32_t aes_rot_word(uint32_t w)
{
    return (w << 8) | (w >> 24);
}

void aes128_init(aes128_ctx_t *ctx, const uint8_t key[AES_KEY_SIZE])
{
    /* First 4 words come directly from the key */
    for (int i = 0; i < 4; i++) {
        ctx->rk[i] = aes_word(key[4 * i], key[4 * i + 1],
                               key[4 * i + 2], key[4 * i + 3]);
    }

    /* Expand remaining round keys */
    for (int i = 4; i < 44; i++) {
        uint32_t temp = ctx->rk[i - 1];
        if ((i % 4) == 0) {
            temp = aes_sub_word(aes_rot_word(temp));
            temp ^= ((uint32_t)aes_rcon[i / 4 - 1]) << 24;
        }
        ctx->rk[i] = ctx->rk[i - 4] ^ temp;
    }
}

/* ===================================================================
 * AES-128 Block Encrypt
 * =================================================================== */

/* GF(2^8) multiply by 2 (xtime) */
static inline uint8_t xtime(uint8_t x)
{
    return (uint8_t)((x << 1) ^ (((x >> 7) & 1) * 0x1b));
}

void aes128_encrypt_block(const aes128_ctx_t *ctx,
                           const uint8_t in[AES_BLOCK_SIZE],
                           uint8_t out[AES_BLOCK_SIZE])
{
    uint8_t state[4][4];

    /* Load input into state (column-major) */
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            state[r][c] = in[c * 4 + r];

    /* Initial AddRoundKey */
    for (int c = 0; c < 4; c++) {
        uint32_t rk = ctx->rk[c];
        state[0][c] ^= aes_byte(rk, 0);
        state[1][c] ^= aes_byte(rk, 1);
        state[2][c] ^= aes_byte(rk, 2);
        state[3][c] ^= aes_byte(rk, 3);
    }

    /* Rounds 1..9 */
    for (int round = 1; round <= 9; round++) {
        uint8_t tmp[4][4];

        /* SubBytes */
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                tmp[r][c] = aes_sbox[state[r][c]];

        /* ShiftRows */
        /* Row 0: no shift */
        state[0][0] = tmp[0][0]; state[0][1] = tmp[0][1];
        state[0][2] = tmp[0][2]; state[0][3] = tmp[0][3];
        /* Row 1: shift left by 1 */
        state[1][0] = tmp[1][1]; state[1][1] = tmp[1][2];
        state[1][2] = tmp[1][3]; state[1][3] = tmp[1][0];
        /* Row 2: shift left by 2 */
        state[2][0] = tmp[2][2]; state[2][1] = tmp[2][3];
        state[2][2] = tmp[2][0]; state[2][3] = tmp[2][1];
        /* Row 3: shift left by 3 */
        state[3][0] = tmp[3][3]; state[3][1] = tmp[3][0];
        state[3][2] = tmp[3][1]; state[3][3] = tmp[3][2];

        /* MixColumns */
        for (int c = 0; c < 4; c++) {
            uint8_t s0 = state[0][c], s1 = state[1][c];
            uint8_t s2 = state[2][c], s3 = state[3][c];
            uint8_t h = s0 ^ s1 ^ s2 ^ s3;
            state[0][c] = s0 ^ xtime(s0 ^ s1) ^ h;
            state[1][c] = s1 ^ xtime(s1 ^ s2) ^ h;
            state[2][c] = s2 ^ xtime(s2 ^ s3) ^ h;
            state[3][c] = s3 ^ xtime(s3 ^ s0) ^ h;
        }

        /* AddRoundKey */
        for (int c = 0; c < 4; c++) {
            uint32_t rk = ctx->rk[round * 4 + c];
            state[0][c] ^= aes_byte(rk, 0);
            state[1][c] ^= aes_byte(rk, 1);
            state[2][c] ^= aes_byte(rk, 2);
            state[3][c] ^= aes_byte(rk, 3);
        }
    }

    /* Round 10 (no MixColumns) */
    {
        uint8_t tmp[4][4];

        /* SubBytes */
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                tmp[r][c] = aes_sbox[state[r][c]];

        /* ShiftRows */
        state[0][0] = tmp[0][0]; state[0][1] = tmp[0][1];
        state[0][2] = tmp[0][2]; state[0][3] = tmp[0][3];
        state[1][0] = tmp[1][1]; state[1][1] = tmp[1][2];
        state[1][2] = tmp[1][3]; state[1][3] = tmp[1][0];
        state[2][0] = tmp[2][2]; state[2][1] = tmp[2][3];
        state[2][2] = tmp[2][0]; state[2][3] = tmp[2][1];
        state[3][0] = tmp[3][3]; state[3][1] = tmp[3][0];
        state[3][2] = tmp[3][1]; state[3][3] = tmp[3][2];

        /* AddRoundKey */
        for (int c = 0; c < 4; c++) {
            uint32_t rk = ctx->rk[40 + c];
            state[0][c] ^= aes_byte(rk, 0);
            state[1][c] ^= aes_byte(rk, 1);
            state[2][c] ^= aes_byte(rk, 2);
            state[3][c] ^= aes_byte(rk, 3);
        }
    }

    /* Write state to output (column-major) */
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            out[c * 4 + r] = state[r][c];
}

/* ===================================================================
 * CCM Mode (Counter with CBC-MAC) per RFC 3610
 * =================================================================== */

/*
 * CCM parameters for CCMP (802.11i):
 *   M  = 8  (MIC length)
 *   L  = 2  (length field size => max message 65535 bytes)
 *   Nonce = 13 bytes (15 - L)
 *
 * Flags byte for B0:    (M-2)/2 << 3 | (L-1) = 0x19
 * Flags byte for CTR A: (L-1) = 0x01
 */

#define CCM_FLAGS_B0   0x59  /* Adata=1, M=8 => t=3, L=2 => q=1: 0x40|0x18|0x01 */
#define CCM_FLAGS_CTR  0x01  /* L-1 = 1 */

/* Compute CBC-MAC over B0 || AAD || plaintext */
static void ccm_cbc_mac(const aes128_ctx_t *ctx,
                          const uint8_t nonce[CCMP_NONCE_LEN],
                          const uint8_t *aad, uint32_t aad_len,
                          const uint8_t *data, uint32_t data_len,
                          uint8_t mac[AES_BLOCK_SIZE])
{
    uint8_t block[AES_BLOCK_SIZE];
    uint8_t enc[AES_BLOCK_SIZE];

    /* B0: flags || nonce || message length (2 bytes big-endian) */
    block[0] = CCM_FLAGS_B0;
    memcpy(&block[1], nonce, CCMP_NONCE_LEN);
    block[14] = (uint8_t)((data_len >> 8) & 0xFF);
    block[15] = (uint8_t)(data_len & 0xFF);

    /* Encrypt B0 -> T */
    aes128_encrypt_block(ctx, block, mac);

    /* Process AAD if present.
     * Format: 2-byte length prefix (big-endian) || AAD || zero-padding to block boundary
     */
    if (aad_len > 0) {
        memset(block, 0, AES_BLOCK_SIZE);
        block[0] = (uint8_t)((aad_len >> 8) & 0xFF);
        block[1] = (uint8_t)(aad_len & 0xFF);

        /* First block: 2 bytes of length + up to 14 bytes of AAD */
        uint32_t first = (aad_len < 14) ? aad_len : 14;
        memcpy(&block[2], aad, first);

        /* XOR and encrypt */
        ccmp_xor_block(block, block, mac, AES_BLOCK_SIZE);
        aes128_encrypt_block(ctx, block, mac);

        /* Remaining AAD blocks */
        uint32_t offset = first;
        while (offset < aad_len) {
            memset(block, 0, AES_BLOCK_SIZE);
            uint32_t chunk = aad_len - offset;
            if (chunk > AES_BLOCK_SIZE)
                chunk = AES_BLOCK_SIZE;
            memcpy(block, &aad[offset], chunk);

            ccmp_xor_block(block, block, mac, AES_BLOCK_SIZE);
            aes128_encrypt_block(ctx, block, mac);

            offset += chunk;
        }
    }

    /* Process plaintext/data blocks */
    uint32_t offset = 0;
    while (offset < data_len) {
        memset(block, 0, AES_BLOCK_SIZE);
        uint32_t chunk = data_len - offset;
        if (chunk > AES_BLOCK_SIZE)
            chunk = AES_BLOCK_SIZE;
        memcpy(block, &data[offset], chunk);

        ccmp_xor_block(block, block, mac, AES_BLOCK_SIZE);
        aes128_encrypt_block(ctx, block, mac);

        offset += chunk;
    }

    (void)enc;
}

/* Generate CTR counter block Ai */
static void ccm_ctr_block(const uint8_t nonce[CCMP_NONCE_LEN],
                            uint16_t counter,
                            uint8_t block[AES_BLOCK_SIZE])
{
    block[0] = CCM_FLAGS_CTR;
    memcpy(&block[1], nonce, CCMP_NONCE_LEN);
    block[14] = (uint8_t)((counter >> 8) & 0xFF);
    block[15] = (uint8_t)(counter & 0xFF);
}

/* ---- Public CCM API ---- */

hal_status_t ccm_encrypt(const aes128_ctx_t *ctx,
                          const uint8_t nonce[CCMP_NONCE_LEN],
                          const uint8_t *aad, uint32_t aad_len,
                          const uint8_t *plaintext, uint32_t pt_len,
                          uint8_t *ciphertext)
{
    uint8_t mac[AES_BLOCK_SIZE];
    uint8_t ctr[AES_BLOCK_SIZE];
    uint8_t keystream[AES_BLOCK_SIZE];

    /* Step 1: Compute CBC-MAC over plaintext */
    ccm_cbc_mac(ctx, nonce, aad, aad_len, plaintext, pt_len, mac);

    /* Step 2: Encrypt plaintext with CTR mode (counters 1, 2, ...) */
    uint32_t offset = 0;
    uint16_t counter = 1;
    while (offset < pt_len) {
        ccm_ctr_block(nonce, counter, ctr);
        aes128_encrypt_block(ctx, ctr, keystream);

        uint32_t chunk = pt_len - offset;
        if (chunk > AES_BLOCK_SIZE)
            chunk = AES_BLOCK_SIZE;

        ccmp_xor_block(&ciphertext[offset], &plaintext[offset],
                        keystream, chunk);

        offset += chunk;
        counter++;
    }

    /* Step 3: Encrypt MAC with CTR counter 0 and append */
    ccm_ctr_block(nonce, 0, ctr);
    aes128_encrypt_block(ctx, ctr, keystream);

    ccmp_xor_block(&ciphertext[pt_len], mac, keystream, CCMP_MIC_LEN);

    return HAL_OK;
}

hal_status_t ccm_decrypt(const aes128_ctx_t *ctx,
                          const uint8_t nonce[CCMP_NONCE_LEN],
                          const uint8_t *aad, uint32_t aad_len,
                          const uint8_t *ciphertext, uint32_t ct_len,
                          uint8_t *plaintext)
{
    if (ct_len < CCMP_MIC_LEN)
        return HAL_ERROR;

    uint32_t pt_len = ct_len - CCMP_MIC_LEN;
    uint8_t mac[AES_BLOCK_SIZE];
    uint8_t ctr[AES_BLOCK_SIZE];
    uint8_t keystream[AES_BLOCK_SIZE];
    uint8_t expected_mic[CCMP_MIC_LEN];

    /* Step 1: Decrypt ciphertext with CTR mode (counters 1, 2, ...) */
    uint32_t offset = 0;
    uint16_t counter = 1;
    while (offset < pt_len) {
        ccm_ctr_block(nonce, counter, ctr);
        aes128_encrypt_block(ctx, ctr, keystream);

        uint32_t chunk = pt_len - offset;
        if (chunk > AES_BLOCK_SIZE)
            chunk = AES_BLOCK_SIZE;

        ccmp_xor_block(&plaintext[offset], &ciphertext[offset],
                        keystream, chunk);

        offset += chunk;
        counter++;
    }

    /* Step 2: Decrypt the received MIC with CTR counter 0 */
    ccm_ctr_block(nonce, 0, ctr);
    aes128_encrypt_block(ctx, ctr, keystream);

    uint8_t recv_mic[CCMP_MIC_LEN];
    ccmp_xor_block(recv_mic, &ciphertext[pt_len], keystream, CCMP_MIC_LEN);

    /* Step 3: Compute CBC-MAC over decrypted plaintext */
    ccm_cbc_mac(ctx, nonce, aad, aad_len, plaintext, pt_len, mac);

    /* Step 4: Compare MICs (constant-time) */
    uint8_t diff = 0;
    for (uint32_t i = 0; i < CCMP_MIC_LEN; i++)
        diff |= recv_mic[i] ^ mac[i];

    (void)expected_mic;

    return (diff == 0) ? HAL_OK : HAL_ERROR;
}

/* ===================================================================
 * CCMP 802.11 helpers
 * =================================================================== */

void ccmp_build_nonce(uint8_t priority, const uint8_t addr2[6],
                       const uint8_t pn[CCMP_PN_LEN],
                       uint8_t nonce[CCMP_NONCE_LEN])
{
    /* Nonce = Priority (1 byte) || A2 (6 bytes) || PN (6 bytes) */
    nonce[0] = priority & 0x0F;
    memcpy(&nonce[1], addr2, 6);
    /* PN in nonce is MSB-first (PN5..PN0) */
    nonce[7]  = pn[5];
    nonce[8]  = pn[4];
    nonce[9]  = pn[3];
    nonce[10] = pn[2];
    nonce[11] = pn[1];
    nonce[12] = pn[0];
}

uint32_t ccmp_build_aad(const uint8_t *hdr, uint32_t hdr_len,
                          uint8_t *aad)
{
    /*
     * AAD for CCMP = masked FC || A1 || A2 || masked SC || [A3] || [A4]
     *
     * FC mask: clear Retry, PwrMgt, MoreData, Protected, Order bits
     *          keep Subtype, Type, Protocol, ToDS, FromDS, MoreFrag
     *
     * SC mask: clear sequence number, keep fragment number
     */
    uint32_t aad_len = 0;

    if (hdr_len < 24)
        return 0;

    /* Frame Control (2 bytes) -- mask out bits we don't authenticate */
    uint16_t fc = (uint16_t)hdr[0] | ((uint16_t)hdr[1] << 8);
    fc &= ~(uint16_t)0x70CE;  /* Clear Retry(11), PwrMgt(12), MoreData(13), Protected(14), Order(15) + subtype bits 4-6 */
    /* Actually per 802.11i, mask = FC & 0x8F8F:
     * Keep: Protocol(0-1), Type(2-3), Subtype(4-7) in byte 0
     * Mask out: Retry, PwrMgt, MoreData bits in byte 1
     * But keep ToDS, FromDS, MoreFrag
     */
    fc = (uint16_t)hdr[0] | ((uint16_t)hdr[1] << 8);
    /* Mask: clear bit 4,5,6 (Retry=11, PwrMgmt=12, MoreData=13) of byte 1
     * and clear Protected bit (bit 14) and Order bit (bit 15)
     * Bit numbering: FC[0..15] where [0]=ProtocolVersion LSB
     * Retry=bit11, PwrMgt=bit12, MoreData=bit13, Protected=bit14, Order=bit15
     */
    fc &= 0x038F;  /* Keep: Protocol(0-1), Type(2-3), Subtype(4-7 = bits 4-7),
                       ToDS(8), FromDS(9), MoreFrag(10) */
    /* Actually the standard mask for CCMP AAD:
     * byte 0: keep all (protocol version, type, subtype)
     * byte 1: clear retry(bit 3), pwrmgt(bit 4), moredata(bit 5), protected(bit 6), order(bit 7)
     * So mask byte 1 with 0x07
     */
    aad[0] = hdr[0] & 0xFF;       /* FC byte 0: all bits kept */
    aad[1] = hdr[1] & 0xC7;       /* FC byte 1: mask retry, pwrmgt, moredata */
    /* Note: bit 6 (Protected) will be set to 0 in the AAD even though
     * it's set in the actual frame. Actually per spec we mask with 0x8F
     * to keep ToDS, FromDS, MoreFrag. Let me use the correct CCMP mask. */
    aad[0] = hdr[0];              /* All of byte 0 */
    aad[1] = hdr[1] & 0xC7;      /* Mask: keep bits 0-2 (ToDS, FromDS, MoreFrag)
                                     and bits 6-7, clear bits 3-5 (Retry, PwrMgt, MoreData) */

    /* Addresses: A1, A2, A3 (18 bytes) */
    memcpy(&aad[2], &hdr[4], 18);  /* A1(6) + A2(6) + A3(6) at hdr offsets 4..21 */

    /* Sequence Control (2 bytes) -- mask sequence number, keep fragment */
    aad[20] = hdr[22] & 0x0F;    /* Fragment number only (low nibble) */
    aad[21] = 0;                  /* Sequence number masked to 0 */

    aad_len = 22;

    /* If 4-address frame (ToDS=1 && FromDS=1), include A4 */
    if ((hdr[1] & 0x03) == 0x03 && hdr_len >= 30) {
        memcpy(&aad[22], &hdr[24], 6);
        aad_len = 28;
    }

    /* If QoS frame (subtype bit 7 set => type/subtype indicates QoS data),
     * include QoS TID field */
    uint8_t type = (hdr[0] >> 2) & 0x03;
    uint8_t subtype = (hdr[0] >> 4) & 0x0F;
    if (type == 2 && (subtype & 0x08)) {
        /* QoS data frame -- QoS Control is after A3 (or A4) */
        uint32_t qos_offset = ((hdr[1] & 0x03) == 0x03) ? 30 : 24;
        if (hdr_len > qos_offset + 1) {
            aad[aad_len] = hdr[qos_offset] & 0x0F;  /* TID only */
            aad[aad_len + 1] = 0;
            aad_len += 2;
        }
    }

    return aad_len;
}

void ccmp_build_hdr(const uint8_t pn[CCMP_PN_LEN], uint8_t key_id,
                     uint8_t out[CCMP_HDR_LEN])
{
    /* CCMP header layout (8 bytes):
     * [0]   = PN0
     * [1]   = PN1
     * [2]   = Reserved (0)
     * [3]   = ExtIV(1) | KeyID(bits 6-7) | Reserved(bits 0-4)
     * [4]   = PN2
     * [5]   = PN3
     * [6]   = PN4
     * [7]   = PN5
     */
    out[0] = pn[0];
    out[1] = pn[1];
    out[2] = 0;
    out[3] = (uint8_t)(0x20 | ((key_id & 0x03) << 6));  /* ExtIV = 1 (bit 5) */
    out[4] = pn[2];
    out[5] = pn[3];
    out[6] = pn[4];
    out[7] = pn[5];
}

void ccmp_parse_hdr(const uint8_t hdr[CCMP_HDR_LEN],
                     uint8_t pn[CCMP_PN_LEN], uint8_t *key_id)
{
    pn[0] = hdr[0];
    pn[1] = hdr[1];
    pn[2] = hdr[4];
    pn[3] = hdr[5];
    pn[4] = hdr[6];
    pn[5] = hdr[7];
    *key_id = (hdr[3] >> 6) & 0x03;
}

/* Determine MAC header length from frame control */
static uint32_t ccmp_mac_hdr_len(const uint8_t *frame)
{
    uint32_t len = 24;  /* Base: FC + Duration + A1 + A2 + A3 + SC */

    /* 4-address? (ToDS && FromDS) */
    if ((frame[1] & 0x03) == 0x03)
        len += 6;  /* A4 */

    /* QoS data? */
    uint8_t type = (frame[0] >> 2) & 0x03;
    uint8_t subtype = (frame[0] >> 4) & 0x0F;
    if (type == 2 && (subtype & 0x08))
        len += 2;  /* QoS Control */

    return len;
}

hal_status_t ccmp_encrypt_frame(const uint8_t tk[AES_KEY_SIZE],
                                 const uint8_t *frame, uint32_t frame_len,
                                 const uint8_t pn[CCMP_PN_LEN],
                                 uint8_t *out, uint32_t *out_len)
{
    uint32_t hdr_len = ccmp_mac_hdr_len(frame);

    if (frame_len < hdr_len)
        return HAL_ERROR;

    uint32_t payload_len = frame_len - hdr_len;

    /* Initialize AES key schedule */
    aes128_ctx_t aes_ctx;
    aes128_init(&aes_ctx, tk);

    /* Build nonce */
    uint8_t nonce[CCMP_NONCE_LEN];
    uint8_t priority = 0;
    /* Extract TID from QoS if present */
    uint8_t type = (frame[0] >> 2) & 0x03;
    uint8_t subtype = (frame[0] >> 4) & 0x0F;
    if (type == 2 && (subtype & 0x08)) {
        uint32_t qos_off = ((frame[1] & 0x03) == 0x03) ? 30 : 24;
        if (frame_len > qos_off)
            priority = frame[qos_off] & 0x0F;
    }
    ccmp_build_nonce(priority, &frame[10], pn, nonce);  /* A2 at offset 10 */

    /* Build AAD */
    uint8_t aad[32];
    uint32_t aad_len = ccmp_build_aad(frame, hdr_len, aad);

    /* Copy MAC header to output (with Protected bit set) */
    memcpy(out, frame, hdr_len);
    out[1] |= 0x40;  /* Set Protected Frame bit */

    /* Insert CCMP header after MAC header */
    ccmp_build_hdr(pn, 0, &out[hdr_len]);

    /* Encrypt payload and generate MIC */
    hal_status_t st = ccm_encrypt(&aes_ctx, nonce, aad, aad_len,
                                   &frame[hdr_len], payload_len,
                                   &out[hdr_len + CCMP_HDR_LEN]);

    *out_len = hdr_len + CCMP_HDR_LEN + payload_len + CCMP_MIC_LEN;
    return st;
}

hal_status_t ccmp_decrypt_frame(const uint8_t tk[AES_KEY_SIZE],
                                 const uint8_t *frame, uint32_t frame_len,
                                 uint8_t *out, uint32_t *out_len)
{
    uint32_t hdr_len = ccmp_mac_hdr_len(frame);

    if (frame_len < hdr_len + CCMP_HDR_LEN + CCMP_MIC_LEN)
        return HAL_ERROR;

    uint32_t enc_payload_len = frame_len - hdr_len - CCMP_HDR_LEN;
    /* enc_payload_len includes MIC */

    /* Parse CCMP header */
    uint8_t pn[CCMP_PN_LEN];
    uint8_t key_id;
    ccmp_parse_hdr(&frame[hdr_len], pn, &key_id);

    /* Initialize AES */
    aes128_ctx_t aes_ctx;
    aes128_init(&aes_ctx, tk);

    /* Build nonce */
    uint8_t nonce[CCMP_NONCE_LEN];
    uint8_t priority = 0;
    uint8_t type = (frame[0] >> 2) & 0x03;
    uint8_t subtype = (frame[0] >> 4) & 0x0F;
    if (type == 2 && (subtype & 0x08)) {
        uint32_t qos_off = ((frame[1] & 0x03) == 0x03) ? 30 : 24;
        if (frame_len > qos_off)
            priority = frame[qos_off] & 0x0F;
    }
    ccmp_build_nonce(priority, &frame[10], pn, nonce);

    /* Build AAD from the header (with Protected bit as-is) */
    uint8_t aad[32];
    uint32_t aad_len = ccmp_build_aad(frame, hdr_len, aad);

    /* Copy MAC header to output (clear Protected bit) */
    memcpy(out, frame, hdr_len);
    out[1] &= ~0x40;  /* Clear Protected Frame bit */

    /* Decrypt and verify MIC */
    hal_status_t st = ccm_decrypt(&aes_ctx, nonce, aad, aad_len,
                                   &frame[hdr_len + CCMP_HDR_LEN],
                                   enc_payload_len,
                                   &out[hdr_len]);

    if (st == HAL_OK)
        *out_len = hdr_len + (enc_payload_len - CCMP_MIC_LEN);
    else
        *out_len = 0;

    return st;
}
