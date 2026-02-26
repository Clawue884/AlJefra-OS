/* SPDX-License-Identifier: MIT */
/* AlJefra OS — Package Signature Verification
 *
 * Implements Ed25519 signature verification for .ajdrv driver packages.
 *
 * Ed25519 is used because:
 *   - 32-byte keys, 64-byte signatures (small)
 *   - Fast verification (~70K verifications/sec on modern CPUs)
 *   - No NIST curves, no patents
 *   - Deterministic signatures (no RNG needed for signing)
 *
 * Full Ed25519 verification implementation for freestanding use.
 * Field arithmetic uses 5-limb (51-bit) representation over GF(2^255-19).
 * Based on the Ed25519 specification (RFC 8032).
 */

#include "verify.h"
#include "package.h"
#include "../hal/hal.h"

/* ── Trusted public key (set at build time or by marketplace) ── */
static uint8_t g_trusted_key[AJDRV_PUBKEY_SIZE];
static int g_key_set;

void ajdrv_set_trusted_key(const uint8_t pub_key[AJDRV_PUBKEY_SIZE])
{
    for (int i = 0; i < AJDRV_PUBKEY_SIZE; i++)
        g_trusted_key[i] = pub_key[i];
    g_key_set = 1;
}

/* ── SHA-512 (needed for Ed25519 verification) ── */

typedef struct {
    uint64_t state[8];
    uint64_t count;
    uint8_t  buffer[128];
    uint32_t buf_len;
} sha512_ctx_t;

static const uint64_t sha512_k[80] = {
    0x428A2F98D728AE22ULL, 0x7137449123EF65CDULL, 0xB5C0FBCFEC4D3B2FULL,
    0xE9B5DBA58189DBBCULL, 0x3956C25BF348B538ULL, 0x59F111F1B605D019ULL,
    0x923F82A4AF194F9BULL, 0xAB1C5ED5DA6D8118ULL, 0xD807AA98A3030242ULL,
    0x12835B0145706FBEULL, 0x243185BE4EE4B28CULL, 0x550C7DC3D5FFB4E2ULL,
    0x72BE5D74F27B896FULL, 0x80DEB1FE3B1696B1ULL, 0x9BDC06A725C71235ULL,
    0xC19BF174CF692694ULL, 0xE49B69C19EF14AD2ULL, 0xEFBE4786384F25E3ULL,
    0x0FC19DC68B8CD5B5ULL, 0x240CA1CC77AC9C65ULL, 0x2DE92C6F592B0275ULL,
    0x4A7484AA6EA6E483ULL, 0x5CB0A9DCBD41FBD4ULL, 0x76F988DA831153B5ULL,
    0x983E5152EE66DFABULL, 0xA831C66D2DB43210ULL, 0xB00327C898FB213FULL,
    0xBF597FC7BEEF0EE4ULL, 0xC6E00BF33DA88FC2ULL, 0xD5A79147930AA725ULL,
    0x06CA6351E003826FULL, 0x142929670A0E6E70ULL, 0x27B70A8546D22FFCULL,
    0x2E1B21385C26C926ULL, 0x4D2C6DFC5AC42AEDULL, 0x53380D139D95B3DFULL,
    0x650A73548BAF63DEULL, 0x766A0ABB3C77B2A8ULL, 0x81C2C92E47EDAEE6ULL,
    0x92722C851482353BULL, 0xA2BFE8A14CF10364ULL, 0xA81A664BBC423001ULL,
    0xC24B8B70D0F89791ULL, 0xC76C51A30654BE30ULL, 0xD192E819D6EF5218ULL,
    0xD69906245565A910ULL, 0xF40E35855771202AULL, 0x106AA07032BBD1B8ULL,
    0x19A4C116B8D2D0C8ULL, 0x1E376C085141AB53ULL, 0x2748774CDF8EEB99ULL,
    0x34B0BCB5E19B48A8ULL, 0x391C0CB3C5C95A63ULL, 0x4ED8AA4AE3418ACBULL,
    0x5B9CCA4F7763E373ULL, 0x682E6FF3D6B2B8A3ULL, 0x748F82EE5DEFB2FCULL,
    0x78A5636F43172F60ULL, 0x84C87814A1F0AB72ULL, 0x8CC702081A6439ECULL,
    0x90BEFFFA23631E28ULL, 0xA4506CEBDE82BDE9ULL, 0xBEF9A3F7B2C67915ULL,
    0xC67178F2E372532BULL, 0xCA273ECEEA26619CULL, 0xD186B8C721C0C207ULL,
    0xEADA7DD6CDE0EB1EULL, 0xF57D4F7FEE6ED178ULL, 0x06F067AA72176FBAULL,
    0x0A637DC5A2C898A6ULL, 0x113F9804BEF90DAEULL, 0x1B710B35131C471BULL,
    0x28DB77F523047D84ULL, 0x32CAAB7B40C72493ULL, 0x3C9EBE0A15C9BEBCULL,
    0x431D67C49C100D4CULL, 0x4CC5D4BECB3E42B6ULL, 0x597F299CFC657E2AULL,
    0x5FCB6FAB3AD6FAECULL, 0x6C44198C4A475817ULL,
};

static uint64_t rotr64(uint64_t x, int n) { return (x >> n) | (x << (64 - n)); }

static void sha512_compress(sha512_ctx_t *ctx, const uint8_t block[128])
{
    uint64_t w[80];
    for (int i = 0; i < 16; i++) {
        w[i] = 0;
        for (int j = 0; j < 8; j++)
            w[i] = (w[i] << 8) | block[i * 8 + j];
    }
    for (int i = 16; i < 80; i++) {
        uint64_t s0 = rotr64(w[i-15], 1) ^ rotr64(w[i-15], 8) ^ (w[i-15] >> 7);
        uint64_t s1 = rotr64(w[i-2], 19) ^ rotr64(w[i-2], 61) ^ (w[i-2] >> 6);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }

    uint64_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3];
    uint64_t e = ctx->state[4], f = ctx->state[5], g = ctx->state[6], h = ctx->state[7];

    for (int i = 0; i < 80; i++) {
        uint64_t S1 = rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41);
        uint64_t ch = (e & f) ^ (~e & g);
        uint64_t temp1 = h + S1 + ch + sha512_k[i] + w[i];
        uint64_t S0 = rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39);
        uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint64_t temp2 = S0 + maj;

        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void sha512_init(sha512_ctx_t *ctx)
{
    ctx->state[0] = 0x6A09E667F3BCC908ULL;
    ctx->state[1] = 0xBB67AE8584CAA73BULL;
    ctx->state[2] = 0x3C6EF372FE94F82BULL;
    ctx->state[3] = 0xA54FF53A5F1D36F1ULL;
    ctx->state[4] = 0x510E527FADE682D1ULL;
    ctx->state[5] = 0x9B05688C2B3E6C1FULL;
    ctx->state[6] = 0x1F83D9ABFB41BD6BULL;
    ctx->state[7] = 0x5BE0CD19137E2179ULL;
    ctx->count = 0;
    ctx->buf_len = 0;
}

static void sha512_update(sha512_ctx_t *ctx, const uint8_t *data, uint64_t len)
{
    while (len > 0) {
        uint32_t space = 128 - ctx->buf_len;
        uint32_t chunk = (len < space) ? (uint32_t)len : space;
        for (uint32_t i = 0; i < chunk; i++)
            ctx->buffer[ctx->buf_len + i] = data[i];
        ctx->buf_len += chunk;
        data += chunk;
        len -= chunk;
        ctx->count += chunk;

        if (ctx->buf_len == 128) {
            sha512_compress(ctx, ctx->buffer);
            ctx->buf_len = 0;
        }
    }
}

static void sha512_final(sha512_ctx_t *ctx, uint8_t hash[64])
{
    uint64_t bits = ctx->count * 8;

    /* Padding */
    ctx->buffer[ctx->buf_len++] = 0x80;
    if (ctx->buf_len > 112) {
        while (ctx->buf_len < 128) ctx->buffer[ctx->buf_len++] = 0;
        sha512_compress(ctx, ctx->buffer);
        ctx->buf_len = 0;
    }
    while (ctx->buf_len < 112) ctx->buffer[ctx->buf_len++] = 0;

    /* Length in big-endian (128-bit, but we only use lower 64 bits) */
    for (int i = 0; i < 8; i++)
        ctx->buffer[112 + i] = 0;
    for (int i = 0; i < 8; i++)
        ctx->buffer[120 + i] = (uint8_t)(bits >> (56 - i * 8));

    sha512_compress(ctx, ctx->buffer);

    /* Output */
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++)
            hash[i * 8 + j] = (uint8_t)(ctx->state[i] >> (56 - j * 8));
}

/* Convenience: hash arbitrary data in one call */
static void sha512(const uint8_t *data, uint64_t len, uint8_t hash[64])
{
    sha512_ctx_t ctx;
    sha512_init(&ctx);
    sha512_update(&ctx, data, len);
    sha512_final(&ctx, hash);
    (void)sha512; /* suppress unused warning when not called directly */
}

/* ════════════════════════════════════════════════════════════════════
 * Ed25519 Signature Verification
 *
 * Curve: -x^2 + y^2 = 1 + d*x^2*y^2  (twisted Edwards)
 * Prime: p = 2^255 - 19
 * Base point order: L = 2^252 + 27742317777372353535851937790883648493
 * d = -121665/121666 mod p
 *
 * Field elements are represented as 5 limbs of 51 bits each (fe[5]).
 * Point representation: extended coordinates (X, Y, Z, T) where
 *   x = X/Z, y = Y/Z, x*y = T/Z.
 *
 * Verification equation: [8][S]B = [8]R + [8][h]A
 * (We use cofactored verification for safety.)
 * ════════════════════════════════════════════════════════════════════ */

/* ── Field element: 5 limbs of 51 bits ── */
typedef int64_t fe[5];

/* 2^51 */
#define FE_LIMB_BITS 51
#define FE_LIMB_MASK ((1LL << FE_LIMB_BITS) - 1)  /* 0x7FFFFFFFFFFFF */

static void fe_0(fe h)
{
    h[0] = 0; h[1] = 0; h[2] = 0; h[3] = 0; h[4] = 0;
}

static void fe_1(fe h)
{
    h[0] = 1; h[1] = 0; h[2] = 0; h[3] = 0; h[4] = 0;
}

static void fe_copy(fe h, const fe f)
{
    h[0] = f[0]; h[1] = f[1]; h[2] = f[2]; h[3] = f[3]; h[4] = f[4];
}

/* Load 32 bytes (little-endian) into a field element, reduce mod p */
static void fe_frombytes(fe h, const uint8_t s[32])
{
    /* Load into 5 limbs of 51 bits from 32 bytes (256 bits) */
    uint64_t h0 = (uint64_t)s[ 0]       | ((uint64_t)s[ 1] <<  8) |
                  ((uint64_t)s[ 2] << 16) | ((uint64_t)s[ 3] << 24) |
                  ((uint64_t)s[ 4] << 32) | ((uint64_t)s[ 5] << 40) |
                  ((uint64_t)(s[ 6] & 0x07) << 48);

    uint64_t h1 = ((uint64_t)s[ 6] >>  3) | ((uint64_t)s[ 7] <<  5) |
                  ((uint64_t)s[ 8] << 13) | ((uint64_t)s[ 9] << 21) |
                  ((uint64_t)s[10] << 29) | ((uint64_t)s[11] << 37) |
                  ((uint64_t)(s[12] & 0x3F) << 45);

    uint64_t h2 = ((uint64_t)s[12] >>  6) | ((uint64_t)s[13] <<  2) |
                  ((uint64_t)s[14] << 10) | ((uint64_t)s[15] << 18) |
                  ((uint64_t)s[16] << 26) | ((uint64_t)s[17] << 34) |
                  ((uint64_t)(s[18] & 0x01) << 42);

    uint64_t h3 = ((uint64_t)s[18] >>  1) | ((uint64_t)s[19] <<  7) |
                  ((uint64_t)s[20] << 15) | ((uint64_t)s[21] << 23) |
                  ((uint64_t)s[22] << 31) | ((uint64_t)s[23] << 39) |
                  ((uint64_t)(s[24] & 0x0F) << 47);

    uint64_t h4 = ((uint64_t)s[24] >>  4) | ((uint64_t)s[25] <<  4) |
                  ((uint64_t)s[26] << 12) | ((uint64_t)s[27] << 20) |
                  ((uint64_t)s[28] << 28) | ((uint64_t)s[29] << 36) |
                  ((uint64_t)(s[30] & 0x7F) << 44);

    /* Ignore the top bit of s[31] (used for sign in point encoding) */
    /* h4 already has only 51 bits from bits 204..254 */
    h4 |= ((uint64_t)(s[31] & 0x7F) << 44);
    /* Wait -- we already have s[30] contributing. Let me redo this properly. */

    /* Actually, let's use a cleaner approach: load 256 bits, split into 51-bit limbs */
    /* Re-derive carefully:
     * Limb 0: bits   0..50  (51 bits)
     * Limb 1: bits  51..101 (51 bits)
     * Limb 2: bits 102..152 (51 bits)
     * Limb 3: bits 153..203 (51 bits)
     * Limb 4: bits 204..254 (51 bits, but bit 255 is sign, so 204..254 = 51 bits)
     */

    /* Load 8 bytes at a time, little-endian */
    uint64_t load0 = 0, load1 = 0, load2 = 0, load3 = 0;
    for (int i = 0; i < 8; i++) {
        load0 |= (uint64_t)s[i]      << (i * 8);
        load1 |= (uint64_t)s[i +  8] << (i * 8);
        load2 |= (uint64_t)s[i + 16] << (i * 8);
        load3 |= (uint64_t)s[i + 24] << (i * 8);
    }
    /* Clear bit 255 (sign bit) */
    load3 &= 0x7FFFFFFFFFFFFFFFULL;

    /* Now split 256 bits into 5x51:
     * h0 = bits[0..50]     = load0 & mask
     * h1 = bits[51..101]   = (load0 >> 51) | (load1 << 13)  & mask
     * h2 = bits[102..152]  = (load1 >> 38) | (load2 << 26)  & mask
     * h3 = bits[153..203]  = (load2 >> 25) | (load3 << 39)  & mask
     * h4 = bits[204..254]  = (load3 >> 12) & mask
     */
    h0 = load0 & FE_LIMB_MASK;
    h1 = ((load0 >> 51) | (load1 << 13)) & FE_LIMB_MASK;
    h2 = ((load1 >> 38) | (load2 << 26)) & FE_LIMB_MASK;
    h3 = ((load2 >> 25) | (load3 << 39)) & FE_LIMB_MASK;
    h4 = (load3 >> 12) & FE_LIMB_MASK;

    h[0] = (int64_t)h0;
    h[1] = (int64_t)h1;
    h[2] = (int64_t)h2;
    h[3] = (int64_t)h3;
    h[4] = (int64_t)h4;
}

/* Fully reduce and store a field element as 32 bytes (little-endian) */
static void fe_tobytes(uint8_t s[32], const fe h)
{
    /* First, carry and reduce to get canonical form */
    int64_t t[5];
    t[0] = h[0]; t[1] = h[1]; t[2] = h[2]; t[3] = h[3]; t[4] = h[4];

    /* Carry propagation */
    int64_t c;
    c = t[0] >> 51; t[1] += c; t[0] &= FE_LIMB_MASK;
    c = t[1] >> 51; t[2] += c; t[1] &= FE_LIMB_MASK;
    c = t[2] >> 51; t[3] += c; t[2] &= FE_LIMB_MASK;
    c = t[3] >> 51; t[4] += c; t[3] &= FE_LIMB_MASK;
    c = t[4] >> 51; t[0] += c * 19; t[4] &= FE_LIMB_MASK;
    /* Second round to handle overflow from t[0] += c*19 */
    c = t[0] >> 51; t[1] += c; t[0] &= FE_LIMB_MASK;
    c = t[1] >> 51; t[2] += c; t[1] &= FE_LIMB_MASK;
    c = t[2] >> 51; t[3] += c; t[2] &= FE_LIMB_MASK;
    c = t[3] >> 51; t[4] += c; t[3] &= FE_LIMB_MASK;
    c = t[4] >> 51; t[0] += c * 19; t[4] &= FE_LIMB_MASK;

    /* Now t is in [0, 2p). Subtract p if t >= p.
     * p = 2^255 - 19, in limbs: {0x7FFFFFFFFFFED, 0x7FFFFFFFFFFFF, ...} */
    /* Compute t - p: if result >= 0, use it */
    int64_t u[5];
    u[0] = t[0] + 19;
    c = u[0] >> 51; u[0] &= FE_LIMB_MASK;
    u[1] = t[1] + c; c = u[1] >> 51; u[1] &= FE_LIMB_MASK;
    u[2] = t[2] + c; c = u[2] >> 51; u[2] &= FE_LIMB_MASK;
    u[3] = t[3] + c; c = u[3] >> 51; u[3] &= FE_LIMB_MASK;
    u[4] = t[4] + c - (1LL << 51); /* subtract 2^255 */

    /* If u[4] >= 0 (no borrow), then t >= p, so use u; else use t */
    int64_t mask = u[4] >> 63; /* -1 if borrow (t < p), 0 if no borrow (t >= p) */
    t[0] = (t[0] & mask) | (u[0] & ~mask);
    t[1] = (t[1] & mask) | (u[1] & ~mask);
    t[2] = (t[2] & mask) | (u[2] & ~mask);
    t[3] = (t[3] & mask) | (u[3] & ~mask);
    t[4] = (t[4] & mask) | (u[4] & ~mask);

    /* Pack 5x51 bits into 32 bytes, little-endian */
    /* 256 bits total = 32 bytes */
    uint64_t bits =
        (uint64_t)t[0] |
        ((uint64_t)t[1] << 51);
    s[0]  = (uint8_t)(bits);
    s[1]  = (uint8_t)(bits >> 8);
    s[2]  = (uint8_t)(bits >> 16);
    s[3]  = (uint8_t)(bits >> 24);
    s[4]  = (uint8_t)(bits >> 32);
    s[5]  = (uint8_t)(bits >> 40);
    s[6]  = (uint8_t)(bits >> 48);
    s[7]  = (uint8_t)(bits >> 56);

    bits = ((uint64_t)t[1] >> 13) | ((uint64_t)t[2] << 38);
    s[8]  = (uint8_t)(bits);
    s[9]  = (uint8_t)(bits >> 8);
    s[10] = (uint8_t)(bits >> 16);
    s[11] = (uint8_t)(bits >> 24);
    s[12] = (uint8_t)(bits >> 32);
    s[13] = (uint8_t)(bits >> 40);
    s[14] = (uint8_t)(bits >> 48);
    s[15] = (uint8_t)(bits >> 56);

    bits = ((uint64_t)t[2] >> 26) | ((uint64_t)t[3] << 25);
    s[16] = (uint8_t)(bits);
    s[17] = (uint8_t)(bits >> 8);
    s[18] = (uint8_t)(bits >> 16);
    s[19] = (uint8_t)(bits >> 24);
    s[20] = (uint8_t)(bits >> 32);
    s[21] = (uint8_t)(bits >> 40);
    s[22] = (uint8_t)(bits >> 48);
    s[23] = (uint8_t)(bits >> 56);

    bits = ((uint64_t)t[3] >> 39) | ((uint64_t)t[4] << 12);
    s[24] = (uint8_t)(bits);
    s[25] = (uint8_t)(bits >> 8);
    s[26] = (uint8_t)(bits >> 16);
    s[27] = (uint8_t)(bits >> 24);
    s[28] = (uint8_t)(bits >> 32);
    s[29] = (uint8_t)(bits >> 40);
    s[30] = (uint8_t)(bits >> 48);
    s[31] = (uint8_t)(bits >> 56);
}

/* ── Field arithmetic (mod 2^255 - 19) ── */

/* Carry-propagate after addition/subtraction */
static void fe_carry(fe h)
{
    int64_t c;
    c = h[0] >> 51; h[1] += c; h[0] &= FE_LIMB_MASK;
    c = h[1] >> 51; h[2] += c; h[1] &= FE_LIMB_MASK;
    c = h[2] >> 51; h[3] += c; h[2] &= FE_LIMB_MASK;
    c = h[3] >> 51; h[4] += c; h[3] &= FE_LIMB_MASK;
    c = h[4] >> 51; h[0] += c * 19; h[4] &= FE_LIMB_MASK;
}

static void fe_add(fe h, const fe f, const fe g)
{
    h[0] = f[0] + g[0];
    h[1] = f[1] + g[1];
    h[2] = f[2] + g[2];
    h[3] = f[3] + g[3];
    h[4] = f[4] + g[4];
    /* Carry will happen in mul/sq or explicitly when needed */
}

static void fe_sub(fe h, const fe f, const fe g)
{
    /* Add 2*p to avoid underflow before subtraction */
    h[0] = f[0] - g[0] + 2 * 0x7FFFFFFFFFFEDLL;
    h[1] = f[1] - g[1] + 2 * FE_LIMB_MASK;
    h[2] = f[2] - g[2] + 2 * FE_LIMB_MASK;
    h[3] = f[3] - g[3] + 2 * FE_LIMB_MASK;
    h[4] = f[4] - g[4] + 2 * FE_LIMB_MASK;
    fe_carry(h);
}

static void fe_neg(fe h, const fe f)
{
    fe zero;
    fe_0(zero);
    fe_sub(h, zero, f);
}

/* 128-bit multiply helper using __int128 (GCC/Clang on 64-bit) */
typedef unsigned __int128 uint128_t;

static void fe_mul(fe h, const fe f, const fe g)
{
    /* Schoolbook multiplication of 5x51-bit limbs */
    uint128_t r0, r1, r2, r3, r4;
    int64_t f0 = f[0], f1 = f[1], f2 = f[2], f3 = f[3], f4 = f[4];
    int64_t g0 = g[0], g1 = g[1], g2 = g[2], g3 = g[3], g4 = g[4];

    /* For limbs that wrap around, multiply by 19 (since 2^255 = 19 mod p) */
    int64_t g1_19 = g1 * 19;
    int64_t g2_19 = g2 * 19;
    int64_t g3_19 = g3 * 19;
    int64_t g4_19 = g4 * 19;

    r0 = (uint128_t)f0 * g0 + (uint128_t)f1 * g4_19 + (uint128_t)f2 * g3_19 +
         (uint128_t)f3 * g2_19 + (uint128_t)f4 * g1_19;
    r1 = (uint128_t)f0 * g1 + (uint128_t)f1 * g0 + (uint128_t)f2 * g4_19 +
         (uint128_t)f3 * g3_19 + (uint128_t)f4 * g2_19;
    r2 = (uint128_t)f0 * g2 + (uint128_t)f1 * g1 + (uint128_t)f2 * g0 +
         (uint128_t)f3 * g4_19 + (uint128_t)f4 * g3_19;
    r3 = (uint128_t)f0 * g3 + (uint128_t)f1 * g2 + (uint128_t)f2 * g1 +
         (uint128_t)f3 * g0 + (uint128_t)f4 * g4_19;
    r4 = (uint128_t)f0 * g4 + (uint128_t)f1 * g3 + (uint128_t)f2 * g2 +
         (uint128_t)f3 * g1 + (uint128_t)f4 * g0;

    /* Carry propagation */
    uint128_t carry;
    carry = r0 >> 51; r1 += carry; r0 &= FE_LIMB_MASK;
    carry = r1 >> 51; r2 += carry; r1 &= FE_LIMB_MASK;
    carry = r2 >> 51; r3 += carry; r2 &= FE_LIMB_MASK;
    carry = r3 >> 51; r4 += carry; r3 &= FE_LIMB_MASK;
    carry = r4 >> 51; r0 += carry * 19; r4 &= FE_LIMB_MASK;
    /* One more carry from r0 in case carry*19 overflowed */
    carry = r0 >> 51; r1 += carry; r0 &= FE_LIMB_MASK;

    h[0] = (int64_t)r0;
    h[1] = (int64_t)r1;
    h[2] = (int64_t)r2;
    h[3] = (int64_t)r3;
    h[4] = (int64_t)r4;
}

static void fe_sq(fe h, const fe f)
{
    fe_mul(h, f, f);
}

/* Multiply field element by a small integer */
static void fe_mul_small(fe h, const fe f, int64_t n)
{
    uint128_t r0 = (uint128_t)f[0] * n;
    uint128_t r1 = (uint128_t)f[1] * n;
    uint128_t r2 = (uint128_t)f[2] * n;
    uint128_t r3 = (uint128_t)f[3] * n;
    uint128_t r4 = (uint128_t)f[4] * n;

    uint128_t carry;
    carry = r0 >> 51; r1 += carry; r0 &= FE_LIMB_MASK;
    carry = r1 >> 51; r2 += carry; r1 &= FE_LIMB_MASK;
    carry = r2 >> 51; r3 += carry; r2 &= FE_LIMB_MASK;
    carry = r3 >> 51; r4 += carry; r3 &= FE_LIMB_MASK;
    carry = r4 >> 51; r0 += carry * 19; r4 &= FE_LIMB_MASK;
    carry = r0 >> 51; r1 += carry; r0 &= FE_LIMB_MASK;

    h[0] = (int64_t)r0;
    h[1] = (int64_t)r1;
    h[2] = (int64_t)r2;
    h[3] = (int64_t)r3;
    h[4] = (int64_t)r4;
}

/* fe_pow2523: compute f^(2^252 - 3) — used for square root (since p = 5 mod 8) */
static void fe_pow2523(fe out, const fe z)
{
    fe t0, t1, t2;
    int i;

    /* z^(2^1) */
    fe_sq(t0, z);
    /* z^(2^2) */
    fe_sq(t1, t0);
    fe_sq(t1, t1);
    /* z^(2^2 + 2^0) = z^5 ... actually z^(4+1) */
    fe_mul(t1, z, t1);       /* z^(2^2 + 1) = z^5? No: z * z^4 = z^5 */
    /* z^(2^1 + 2^2 + 2^0)  */
    fe_mul(t0, t0, t1);      /* z^2 * z^5 = z^7? No: t0=z^2, t1=z^5 -> z^7? */

    /* Let me use the standard addition chain for (2^252 - 3):
     * This follows the ref10 implementation. */

    /* t0 = z^(2^1) */
    fe_sq(t0, z);
    /* t1 = z^(2^2) */
    fe_sq(t1, t0);
    fe_sq(t1, t1);
    /* t1 = z^(2^2 + 2^0) */
    fe_mul(t1, z, t1);
    /* t0 = z^(2^2 + 2^1 + 2^0) */
    fe_mul(t0, t0, t1);       /* z^2 * z^5 = z^7 ... nah. */

    /* Actually, let me just use the standard pow2523 addition chain properly.
     * We want z^((p-5)/8) = z^(2^252-3).
     *
     * Standard approach:
     *   2    = sq(1)
     *   4    = sq(2)
     *   5    = 4 * 1
     *   10   = sq(5)
     *   11   = 10 * 1
     *   2^5  - 2^0  = sq^5(11) * 11       -> call this x_5_0
     *   2^10 - 2^0  = sq^5(x_5_0) * x_5_0 -> x_10_0
     *   2^20 - 2^0  = sq^10(x_10_0) * x_10_0  -> x_20_0
     *   2^40 - 2^0  = sq^20(x_20_0) * x_20_0  -> x_40_0
     *   2^50 - 2^0  = sq^10(x_40_0) * x_10_0   -> x_50_0
     *   2^100 - 2^0 = sq^50(x_50_0) * x_50_0   -> x_100_0
     *   2^200 - 2^0 = sq^100(x_100_0) * x_100_0 -> x_200_0
     *   2^250 - 2^0 = sq^50(x_200_0) * x_50_0   -> x_250_0
     *   2^252 - 2^2 = sq^2(x_250_0)
     *   2^252 - 3   = (2^252 - 4) * z = sq^2(x_250_0) * z
     *   Wait, that's 2^252 - 4 + 1 = 2^252 - 3. Yes.
     */

    fe z1;
    fe_copy(z1, z);

    /* z2 = z^2 */
    fe z2;
    fe_sq(z2, z1);

    /* z8 = z^(2^3) */
    fe z8;
    fe_sq(z8, z2);       /* z^4 */
    fe_sq(z8, z8);       /* z^8 */

    /* z9 = z^9 = z^8 * z^1 */
    fe z9;
    fe_mul(z9, z8, z1);

    /* z11 = z^11 = z^9 * z^2 */
    fe z11;
    fe_mul(z11, z9, z2);

    /* t = z^22 */
    fe_sq(t0, z11);

    /* z_5_0 = z^(2^5 - 1) = z^31 = z^22 * z^9 */
    fe z_5_0;
    fe_mul(z_5_0, t0, z9);

    /* z_10_0 = z^(2^10 - 1) */
    fe_sq(t0, z_5_0);
    for (i = 1; i < 5; i++) fe_sq(t0, t0);   /* t0 = z^(2^10 - 2^5) */
    fe z_10_0;
    fe_mul(z_10_0, t0, z_5_0);

    /* z_20_0 = z^(2^20 - 1) */
    fe_sq(t0, z_10_0);
    for (i = 1; i < 10; i++) fe_sq(t0, t0);  /* t0 = z^(2^20 - 2^10) */
    fe z_20_0;
    fe_mul(z_20_0, t0, z_10_0);

    /* z^(2^40 - 1) */
    fe_sq(t0, z_20_0);
    for (i = 1; i < 20; i++) fe_sq(t0, t0);  /* z^(2^40 - 2^20) */
    fe z_40_0;
    fe_mul(z_40_0, t0, z_20_0);

    /* z^(2^50 - 1) */
    fe_sq(t0, z_40_0);
    for (i = 1; i < 10; i++) fe_sq(t0, t0);  /* z^(2^50 - 2^10) */
    fe z_50_0;
    fe_mul(z_50_0, t0, z_10_0);

    /* z^(2^100 - 1) */
    fe_sq(t0, z_50_0);
    for (i = 1; i < 50; i++) fe_sq(t0, t0);  /* z^(2^100 - 2^50) */
    fe z_100_0;
    fe_mul(z_100_0, t0, z_50_0);

    /* z^(2^200 - 1) */
    fe_sq(t0, z_100_0);
    for (i = 1; i < 100; i++) fe_sq(t0, t0); /* z^(2^200 - 2^100) */
    fe_mul(t0, t0, z_100_0);                  /* z^(2^200 - 1) */

    /* z^(2^250 - 1) */
    fe_sq(t0, t0);
    for (i = 1; i < 50; i++) fe_sq(t0, t0);  /* z^(2^250 - 2^50) */
    fe_mul(t0, t0, z_50_0);                   /* z^(2^250 - 1) */

    /* z^(2^252 - 4) */
    fe_sq(t0, t0);
    fe_sq(t0, t0);                             /* z^(2^252 - 4) */

    /* z^(2^252 - 3) */
    fe_mul(out, t0, z1);

    (void)t2;
}

/* fe_invert: compute z^(p-2) = z^(2^255 - 21) via Fermat's little theorem */
static void fe_invert(fe out, const fe z)
{
    fe t0, t1;
    int i;

    fe z1;
    fe_copy(z1, z);

    /* z^2 */
    fe z2;
    fe_sq(z2, z1);

    /* z^(2^3) */
    fe z8;
    fe_sq(z8, z2);
    fe_sq(z8, z8);

    /* z^9 */
    fe z9;
    fe_mul(z9, z8, z1);

    /* z^11 */
    fe z11;
    fe_mul(z11, z9, z2);

    /* z^(2^5 - 2^0) = z^31 ... via z^22 * z^9 */
    fe_sq(t0, z11);
    fe z_5_0;
    fe_mul(z_5_0, t0, z9);

    /* z^(2^10 - 1) */
    fe_sq(t0, z_5_0);
    for (i = 1; i < 5; i++) fe_sq(t0, t0);
    fe z_10_0;
    fe_mul(z_10_0, t0, z_5_0);

    /* z^(2^20 - 1) */
    fe_sq(t0, z_10_0);
    for (i = 1; i < 10; i++) fe_sq(t0, t0);
    fe z_20_0;
    fe_mul(z_20_0, t0, z_10_0);

    /* z^(2^40 - 1) */
    fe_sq(t0, z_20_0);
    for (i = 1; i < 20; i++) fe_sq(t0, t0);
    fe z_40_0;
    fe_mul(z_40_0, t0, z_20_0);

    /* z^(2^50 - 1) */
    fe_sq(t0, z_40_0);
    for (i = 1; i < 10; i++) fe_sq(t0, t0);
    fe z_50_0;
    fe_mul(z_50_0, t0, z_10_0);

    /* z^(2^100 - 1) */
    fe_sq(t0, z_50_0);
    for (i = 1; i < 50; i++) fe_sq(t0, t0);
    fe z_100_0;
    fe_mul(z_100_0, t0, z_50_0);

    /* z^(2^200 - 1) */
    fe_sq(t0, z_100_0);
    for (i = 1; i < 100; i++) fe_sq(t0, t0);
    fe_mul(t0, t0, z_100_0);

    /* z^(2^250 - 1) */
    fe_sq(t0, t0);
    for (i = 1; i < 50; i++) fe_sq(t0, t0);
    fe_mul(t0, t0, z_50_0);

    /* z^(2^255 - 2^5 + 2^4 + ... ) -> we need z^(2^255-21) = z^(p-2)
     * Continue: z^(2^252 - 4) then z^(2^255 - 32) then adjust.
     * Actually, p-2 = 2^255 - 21.
     * From z^(2^250-1):
     *   sq^5 -> z^(2^255 - 2^5) = z^(2^255 - 32)
     *   multiply by z^11 -> z^(2^255 - 21)
     */
    for (i = 0; i < 5; i++) fe_sq(t0, t0);   /* z^(2^255 - 32) */
    fe_mul(out, t0, z11);                      /* z^(2^255 - 21) = z^(p-2) */

    (void)t1;
}

/* Check if field element is zero (after reduction) */
static int fe_isnonzero(const fe f)
{
    uint8_t s[32];
    fe_tobytes(s, f);
    uint8_t r = 0;
    for (int i = 0; i < 32; i++)
        r |= s[i];
    return r != 0;
}

/* Check if field element is negative (LSB of canonical form) */
static int fe_isneg(const fe f)
{
    uint8_t s[32];
    fe_tobytes(s, f);
    return s[0] & 1;
}

/* Conditional move: if b=1, replace f with g; constant-time */
static void fe_cmov(fe f, const fe g, int b)
{
    int64_t mask = -(int64_t)b; /* 0 or -1 */
    f[0] ^= (f[0] ^ g[0]) & mask;
    f[1] ^= (f[1] ^ g[1]) & mask;
    f[2] ^= (f[2] ^ g[2]) & mask;
    f[3] ^= (f[3] ^ g[3]) & mask;
    f[4] ^= (f[4] ^ g[4]) & mask;
}

/* ── Ed25519 curve parameters ── */

/* d = -121665/121666 mod p
 * = 37095705934669439343138083508754565189542113879843219016388785533085940283555
 * In bytes (little-endian): */
static const uint8_t ed25519_d_bytes[32] = {
    0xa3, 0x78, 0x59, 0x13, 0xca, 0x4d, 0xeb, 0x75,
    0xab, 0xd8, 0x41, 0x41, 0x4d, 0x0a, 0x70, 0x00,
    0x98, 0xe8, 0x79, 0x77, 0x79, 0x40, 0xc7, 0x8c,
    0x73, 0xfe, 0x6f, 0x2b, 0xee, 0x6c, 0x03, 0x52
};

/* 2*d */
static const uint8_t ed25519_2d_bytes[32] = {
    0x59, 0xf1, 0xb2, 0x26, 0x94, 0x9b, 0xd6, 0xeb,
    0x56, 0xb1, 0x83, 0x82, 0x9a, 0x14, 0xe0, 0x00,
    0x30, 0xd1, 0xf3, 0xee, 0xf2, 0x80, 0x8e, 0x19,
    0xe7, 0xfc, 0xdf, 0x56, 0xdc, 0xd9, 0x06, 0x24
};

/* sqrt(-1) mod p
 * = 2^((p-1)/4) mod p
 * = 19681161376707505956807079304988542015446066515923890162744021073123829784752 */
static const uint8_t ed25519_sqrtm1_bytes[32] = {
    0xb0, 0xa0, 0x0e, 0x4a, 0x27, 0x1b, 0xee, 0xc4,
    0x78, 0xe4, 0x2f, 0xad, 0x06, 0x18, 0x43, 0x2f,
    0xa7, 0xd7, 0xfb, 0x3d, 0x99, 0x00, 0x4d, 0x2b,
    0x0b, 0xdf, 0xc1, 0x4f, 0x80, 0x24, 0x83, 0x2b
};

/* Ed25519 base point B:
 * y = 4/5 mod p = 46316835694926478169428394003475163141307993866256225615783033890098355573289
 * x is the positive root */
static const uint8_t ed25519_By_bytes[32] = {
    0x58, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
    0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
    0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
    0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66
};

static const uint8_t ed25519_Bx_bytes[32] = {
    0x1a, 0xd5, 0x25, 0x8f, 0x60, 0x2d, 0x56, 0xc9,
    0xb2, 0xa7, 0x25, 0x95, 0x60, 0xc7, 0x2c, 0x69,
    0x5c, 0xdc, 0xd6, 0xfd, 0x31, 0xe2, 0xa4, 0xc0,
    0xfe, 0x53, 0x6e, 0xcd, 0xd3, 0x36, 0x69, 0x21
};

/* Group order L = 2^252 + 27742317777372353535851937790883648493
 * In little-endian bytes: */
static const uint8_t ed25519_L[32] = {
    0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
    0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10
};

/* ── Extended point representation ── */
/* (X : Y : Z : T) where x=X/Z, y=Y/Z, x*y=T/Z */
typedef struct {
    fe X;
    fe Y;
    fe Z;
    fe T;
} ge_p3;

/* Completed point (for intermediate in addition) */
typedef struct {
    fe X;
    fe Y;
    fe Z;
    fe T;
} ge_p1p1;

/* Projective (for doubling without T) */
typedef struct {
    fe X;
    fe Y;
    fe Z;
} ge_p2;

/* Pre-computed point (y+x, y-x, 2*d*x*y) for faster addition */
typedef struct {
    fe ypx;   /* y + x */
    fe ymx;   /* y - x */
    fe xy2d;  /* 2 * d * x * y */
} ge_precomp;

/* Cached (Y+X, Y-X, Z, T*2d) for addition */
typedef struct {
    fe YpX;
    fe YmX;
    fe Z;
    fe T2d;
} ge_cached;

/* ── Point operations ── */

/* Set point to identity (0, 1, 1, 0) */
static void ge_p3_0(ge_p3 *h)
{
    fe_0(h->X);
    fe_1(h->Y);
    fe_1(h->Z);
    fe_0(h->T);
}

/* Convert p3 to cached (for addition) */
static void ge_p3_to_cached(ge_cached *r, const ge_p3 *p)
{
    fe d2;
    fe_frombytes(d2, ed25519_2d_bytes);
    fe_add(r->YpX, p->Y, p->X);
    fe_sub(r->YmX, p->Y, p->X);
    fe_copy(r->Z, p->Z);
    fe_mul(r->T2d, p->T, d2);
}

/* Convert p3 to p2 (drop T) */
static void ge_p3_to_p2(ge_p2 *r, const ge_p3 *p)
{
    fe_copy(r->X, p->X);
    fe_copy(r->Y, p->Y);
    fe_copy(r->Z, p->Z);
}

/* Convert p1p1 (completed) to p3 (extended) */
static void ge_p1p1_to_p3(ge_p3 *r, const ge_p1p1 *p)
{
    fe_mul(r->X, p->X, p->T);
    fe_mul(r->Y, p->Y, p->Z);
    fe_mul(r->Z, p->Z, p->T);
    fe_mul(r->T, p->X, p->Y);
}

/* Convert p1p1 (completed) to p2 (projective) */
static void ge_p1p1_to_p2(ge_p2 *r, const ge_p1p1 *p)
{
    fe_mul(r->X, p->X, p->T);
    fe_mul(r->Y, p->Y, p->Z);
    fe_mul(r->Z, p->Z, p->T);
}

/* Point doubling: p2 -> p1p1
 * Formulas from https://hyperelliptic.org/EFD/g1p/auto-twisted-projective.html
 * Cost: 4S + 3a + 1D (no d needed for doubling on twisted Edwards)
 */
static void ge_p2_dbl(ge_p1p1 *r, const ge_p2 *p)
{
    fe t0;

    fe_sq(r->X, p->X);           /* A = X^2 */
    fe_sq(r->Z, p->Y);           /* B = Y^2 */
    fe_sq(r->T, p->Z);
    fe_add(r->T, r->T, r->T);    /* C = 2*Z^2 */
    fe_neg(t0, r->X);            /* -A (since a = -1 for Ed25519) */
    fe_add(r->Y, p->X, p->Y);
    fe_sq(r->Y, r->Y);           /* (X+Y)^2 */
    fe_sub(r->Y, r->Y, r->X);   /* (X+Y)^2 - A */
    fe_sub(r->Y, r->Y, r->Z);   /* E = (X+Y)^2 - A - B */
    fe_add(r->X, t0, r->Z);     /* G = -A + B */
    fe_sub(r->T, r->X, r->T);   /* F = G - C */
    /* Now: r->X = E, r->Y = G, r->Z = F (we need to rearrange) */
    /* Actually let me redo with named temporaries */
    fe A, B, C, E, F, G, H;
    fe_sq(A, p->X);              /* A = X1^2 */
    fe_sq(B, p->Y);              /* B = Y1^2 */
    fe_sq(C, p->Z);
    fe_add(C, C, C);             /* C = 2 * Z1^2 */
    fe_neg(H, A);                /* H = -A (because a=-1 in Ed25519: a*A = -A) */
    fe_add(E, p->X, p->Y);
    fe_sq(E, E);
    fe_sub(E, E, A);
    fe_sub(E, E, B);             /* E = (X1+Y1)^2 - A - B */
    fe_add(G, H, B);             /* G = -A + B  (= aA + B where a=-1) */
    fe_sub(F, G, C);             /* F = G - C */
    fe_sub(r->T, H, B);
    /* Wait, let me use the standard unified doubling formula for -x^2+y^2=1+dx^2y^2
     *
     * ref10 uses:
     *   A = X1^2
     *   B = Y1^2
     *   C = 2*Z1^2
     *   D = a*A = -A   (for Ed25519, a=-1)
     *   E = (X1+Y1)^2 - A - B
     *   G = D + B
     *   F = G - C
     *   H = D - B
     *   X3 = E * F
     *   Y3 = G * H
     *   T3 = E * H
     *   Z3 = F * G
     */
    fe_sub(H, H, B);  /* H = -A - B = D - B */
    /* p1p1 convention: X3 = r->X * r->T, Y3 = r->Y * r->Z,
     *                  Z3 = r->Z * r->T, T3 = r->X * r->Y
     * We need: X3=E*F, Y3=G*H, Z3=F*G, T3=E*H
     * So: r->X=E, r->Y=H, r->Z=G, r->T=F */
    fe_copy(r->X, E);
    fe_copy(r->Y, H);
    fe_copy(r->Z, G);
    fe_copy(r->T, F);
}

/* Point addition: p3 + cached -> p1p1
 * Uses the unified addition formula for extended coordinates.
 */
static void ge_add(ge_p1p1 *r, const ge_p3 *p, const ge_cached *q)
{
    fe A, B, C, D;

    fe_add(A, p->Y, p->X);       /* A = Y1 + X1 */
    fe_sub(B, p->Y, p->X);       /* B = Y1 - X1 */
    fe_mul(A, A, q->YpX);        /* A = (Y1+X1) * (Y2+X2) */
    fe_mul(B, B, q->YmX);        /* B = (Y1-X1) * (Y2-X2) */
    fe_mul(C, q->T2d, p->T);     /* C = T2d * T1 = 2*d*T1*T2 */
    fe_mul(D, p->Z, q->Z);
    fe_add(D, D, D);              /* D = 2 * Z1 * Z2 */

    fe_sub(r->X, A, B);          /* E = A - B */
    fe_sub(r->T, D, C);          /* F = D - C */
    fe_add(r->Z, D, C);          /* G = D + C */
    fe_add(r->Y, A, B);          /* H = A + B */

    /* X3 = E*F, Y3 = G*H, Z3 = F*G, T3 = E*H (done at p1p1->p3 conversion) */
}

/* Point subtraction: p3 - cached -> p1p1 */
static void ge_sub(ge_p1p1 *r, const ge_p3 *p, const ge_cached *q)
{
    fe A, B, C, D;

    fe_add(A, p->Y, p->X);
    fe_sub(B, p->Y, p->X);
    fe_mul(A, A, q->YmX);        /* Swap: use YmX where add uses YpX */
    fe_mul(B, B, q->YpX);        /* Swap: use YpX where add uses YmX */
    fe_mul(C, q->T2d, p->T);
    fe_mul(D, p->Z, q->Z);
    fe_add(D, D, D);

    fe_sub(r->X, A, B);          /* E = A - B */
    fe_add(r->T, D, C);          /* F = D + C (negated from add) */
    fe_sub(r->Z, D, C);          /* G = D - C (negated from add) */
    fe_add(r->Y, A, B);          /* H = A + B */
}

/* ── Point decompression ── */
/* Recover (X, Y, Z, T) from a 32-byte Ed25519 point encoding.
 * Encoding: 32 bytes, little-endian Y with the sign of X in the top bit.
 * Returns 0 on success, -1 on failure (point not on curve). */
static int ge_frombytes(ge_p3 *h, const uint8_t s[32])
{
    fe u, v, v3, vxx, check;
    fe d;

    fe_frombytes(d, ed25519_d_bytes);

    int x_sign = (s[31] >> 7) & 1;

    /* Y from bytes (clear sign bit) */
    uint8_t y_bytes[32];
    for (int i = 0; i < 32; i++) y_bytes[i] = s[i];
    y_bytes[31] &= 0x7F;
    fe_frombytes(h->Y, y_bytes);

    /* u = y^2 - 1 */
    fe_sq(u, h->Y);
    fe_copy(v, u);               /* v = y^2 (temporary) */
    fe one;
    fe_1(one);
    fe_sub(u, u, one);           /* u = y^2 - 1 */

    /* v = d*y^2 + 1 */
    fe_mul(v, d, v);             /* v = d * y^2 */
    fe_add(v, v, one);           /* v = d*y^2 + 1 */

    /* x = sqrt(u/v) = u * v^3 * (u * v^7)^((p-5)/8)
     *
     * Explanation:
     *   u/v = u * v^(-1)
     *   Since p = 5 (mod 8), we can use:
     *   candidate = (u/v)^((p+3)/8) = u * v^3 * (u*v^7)^((p-5)/8)
     *   Then check if candidate^2 * v == u or == -u
     */

    /* v3 = v^3 */
    fe_sq(v3, v);                /* v^2 */
    fe_mul(v3, v3, v);           /* v^3 */

    /* h->X = u * v^3  (will become x if pow2523 gives right answer) */
    fe_mul(h->X, u, v3);        /* u * v^3 */

    /* vxx = u * v^7 */
    fe v7;
    fe_sq(v7, v3);              /* v^6 */
    fe_mul(v7, v7, v);          /* v^7 */
    fe uv7;
    fe_mul(uv7, u, v7);         /* u * v^7 */

    /* (u*v^7)^((p-5)/8) */
    fe beta;
    fe_pow2523(beta, uv7);

    /* x = u * v^3 * beta */
    fe_mul(h->X, h->X, beta);

    /* Verify: check = x^2 * v - u */
    fe_sq(vxx, h->X);           /* x^2 */
    fe_mul(check, vxx, v);      /* x^2 * v */
    fe_sub(check, check, u);    /* x^2 * v - u */

    if (fe_isnonzero(check)) {
        /* Try x^2 * v + u == 0, i.e., x^2 * v = -u, meaning we need x * sqrt(-1) */
        fe_add(check, vxx, u);
        fe_mul(check, check, v);
        /* Actually, re-check: if x^2*v != u, check if x^2*v == -u */
        fe_sq(vxx, h->X);
        fe_mul(check, vxx, v);
        fe_add(check, check, u);   /* x^2 * v + u */
        if (fe_isnonzero(check)) {
            return -1;  /* Not on curve */
        }
        /* Multiply x by sqrt(-1) */
        fe sqrtm1;
        fe_frombytes(sqrtm1, ed25519_sqrtm1_bytes);
        fe_mul(h->X, h->X, sqrtm1);
    }

    /* Adjust sign of x */
    if (fe_isneg(h->X) != x_sign) {
        fe_neg(h->X, h->X);
    }

    /* Compute T = X * Y (with Z = 1 at this point) */
    fe_1(h->Z);
    fe_mul(h->T, h->X, h->Y);

    return 0;
}

/* Encode a point to 32 bytes (Y with sign of X in top bit) */
static void ge_tobytes(uint8_t s[32], const ge_p3 *h)
{
    fe recip, x, y;

    fe_invert(recip, h->Z);
    fe_mul(x, h->X, recip);
    fe_mul(y, h->Y, recip);
    fe_tobytes(s, y);
    s[31] ^= (uint8_t)(fe_isneg(x) << 7);
}

/* ── Scalar reduction mod L ── */

/* Reduce a 64-byte (512-bit) scalar mod L.
 * L = 2^252 + 27742317777372353535851937790883648493
 * Input: 64-byte little-endian integer from SHA-512 output.
 * Output: 32-byte little-endian integer in [0, L).
 *
 * Based on the SUPERCOP ref10 sc_reduce algorithm: load into 24 x 21-bit
 * signed limbs, then eliminate high limbs using the identity
 * 2^252 = -c (mod L), where c has 21-bit signed representation
 * {-666643, -470296, -654183, +997805, -136657, +683901}.
 */

/* Helper: load 3 bytes little-endian */
static uint64_t sc_load3(const uint8_t *p)
{
    return (uint64_t)p[0] | ((uint64_t)p[1] << 8) | ((uint64_t)p[2] << 16);
}

/* Helper: load 4 bytes little-endian */
static uint64_t sc_load4(const uint8_t *p)
{
    return (uint64_t)p[0] | ((uint64_t)p[1] << 8) |
           ((uint64_t)p[2] << 16) | ((uint64_t)p[3] << 24);
}

static void sc_reduce(uint8_t out[32], const uint8_t in[64])
{
    /* Load 64 bytes into 24 x 21-bit limbs (ref10 layout) */
    int64_t s0  = (int64_t)(2097151 & sc_load3(in +  0));
    int64_t s1  = (int64_t)(2097151 & (sc_load4(in +  2) >>  5));
    int64_t s2  = (int64_t)(2097151 & (sc_load3(in +  5) >>  2));
    int64_t s3  = (int64_t)(2097151 & (sc_load4(in +  7) >>  7));
    int64_t s4  = (int64_t)(2097151 & (sc_load4(in + 10) >>  4));
    int64_t s5  = (int64_t)(2097151 & (sc_load3(in + 13) >>  1));
    int64_t s6  = (int64_t)(2097151 & (sc_load4(in + 15) >>  6));
    int64_t s7  = (int64_t)(2097151 & (sc_load3(in + 18) >>  3));
    int64_t s8  = (int64_t)(2097151 & sc_load3(in + 21));
    int64_t s9  = (int64_t)(2097151 & (sc_load4(in + 23) >>  5));
    int64_t s10 = (int64_t)(2097151 & (sc_load3(in + 26) >>  2));
    int64_t s11 = (int64_t)(2097151 & (sc_load4(in + 28) >>  7));
    int64_t s12 = (int64_t)(2097151 & (sc_load4(in + 31) >>  4));
    int64_t s13 = (int64_t)(2097151 & (sc_load3(in + 34) >>  1));
    int64_t s14 = (int64_t)(2097151 & (sc_load4(in + 36) >>  6));
    int64_t s15 = (int64_t)(2097151 & (sc_load3(in + 39) >>  3));
    int64_t s16 = (int64_t)(2097151 & sc_load3(in + 42));
    int64_t s17 = (int64_t)(2097151 & (sc_load4(in + 44) >>  5));
    int64_t s18 = (int64_t)(2097151 & (sc_load3(in + 47) >>  2));
    int64_t s19 = (int64_t)(2097151 & (sc_load4(in + 49) >>  7));
    int64_t s20 = (int64_t)(2097151 & (sc_load4(in + 52) >>  4));
    int64_t s21 = (int64_t)(2097151 & (sc_load3(in + 55) >>  1));
    int64_t s22 = (int64_t)(2097151 & (sc_load4(in + 57) >>  6));
    int64_t s23 = (int64_t)(sc_load4(in + 60) >>  3);

    int64_t carry0, carry1, carry2, carry3, carry4, carry5;
    int64_t carry6, carry7, carry8, carry9, carry10, carry11;
    int64_t carry12, carry13, carry14, carry15, carry16;

    /* Phase 1: eliminate s23..s18 */
    s11 += s23 * 666643; s12 += s23 * 470296;
    s13 += s23 * 654183; s14 -= s23 * 997805;
    s15 += s23 * 136657; s16 -= s23 * 683901; s23 = 0;

    s10 += s22 * 666643; s11 += s22 * 470296;
    s12 += s22 * 654183; s13 -= s22 * 997805;
    s14 += s22 * 136657; s15 -= s22 * 683901; s22 = 0;

    s9  += s21 * 666643; s10 += s21 * 470296;
    s11 += s21 * 654183; s12 -= s21 * 997805;
    s13 += s21 * 136657; s14 -= s21 * 683901; s21 = 0;

    s8  += s20 * 666643; s9  += s20 * 470296;
    s10 += s20 * 654183; s11 -= s20 * 997805;
    s12 += s20 * 136657; s13 -= s20 * 683901; s20 = 0;

    s7  += s19 * 666643; s8  += s19 * 470296;
    s9  += s19 * 654183; s10 -= s19 * 997805;
    s11 += s19 * 136657; s12 -= s19 * 683901; s19 = 0;

    s6  += s18 * 666643; s7  += s18 * 470296;
    s8  += s18 * 654183; s9  -= s18 * 997805;
    s10 += s18 * 136657; s11 -= s18 * 683901; s18 = 0;

    /* Carry-propagate (even then odd) to keep limbs bounded */
    carry6  = (s6  + (1 << 20)) >> 21; s7  += carry6;  s6  -= carry6  << 21;
    carry8  = (s8  + (1 << 20)) >> 21; s9  += carry8;  s8  -= carry8  << 21;
    carry10 = (s10 + (1 << 20)) >> 21; s11 += carry10; s10 -= carry10 << 21;
    carry12 = (s12 + (1 << 20)) >> 21; s13 += carry12; s12 -= carry12 << 21;
    carry14 = (s14 + (1 << 20)) >> 21; s15 += carry14; s14 -= carry14 << 21;
    carry16 = (s16 + (1 << 20)) >> 21; s17 += carry16; s16 -= carry16 << 21;

    carry7  = (s7  + (1 << 20)) >> 21; s8  += carry7;  s7  -= carry7  << 21;
    carry9  = (s9  + (1 << 20)) >> 21; s10 += carry9;  s9  -= carry9  << 21;
    carry11 = (s11 + (1 << 20)) >> 21; s12 += carry11; s11 -= carry11 << 21;
    carry13 = (s13 + (1 << 20)) >> 21; s14 += carry13; s13 -= carry13 << 21;
    carry15 = (s15 + (1 << 20)) >> 21; s16 += carry15; s15 -= carry15 << 21;

    /* Phase 2: eliminate s17..s12 */
    s5  += s17 * 666643; s6  += s17 * 470296;
    s7  += s17 * 654183; s8  -= s17 * 997805;
    s9  += s17 * 136657; s10 -= s17 * 683901; s17 = 0;

    s4  += s16 * 666643; s5  += s16 * 470296;
    s6  += s16 * 654183; s7  -= s16 * 997805;
    s8  += s16 * 136657; s9  -= s16 * 683901; s16 = 0;

    s3  += s15 * 666643; s4  += s15 * 470296;
    s5  += s15 * 654183; s6  -= s15 * 997805;
    s7  += s15 * 136657; s8  -= s15 * 683901; s15 = 0;

    s2  += s14 * 666643; s3  += s14 * 470296;
    s4  += s14 * 654183; s5  -= s14 * 997805;
    s6  += s14 * 136657; s7  -= s14 * 683901; s14 = 0;

    s1  += s13 * 666643; s2  += s13 * 470296;
    s3  += s13 * 654183; s4  -= s13 * 997805;
    s5  += s13 * 136657; s6  -= s13 * 683901; s13 = 0;

    s0  += s12 * 666643; s1  += s12 * 470296;
    s2  += s12 * 654183; s3  -= s12 * 997805;
    s4  += s12 * 136657; s5  -= s12 * 683901; s12 = 0;

    /* Carry-propagate s0..s11 (even then odd) */
    carry0  = (s0  + (1 << 20)) >> 21; s1  += carry0;  s0  -= carry0  << 21;
    carry2  = (s2  + (1 << 20)) >> 21; s3  += carry2;  s2  -= carry2  << 21;
    carry4  = (s4  + (1 << 20)) >> 21; s5  += carry4;  s4  -= carry4  << 21;
    carry6  = (s6  + (1 << 20)) >> 21; s7  += carry6;  s6  -= carry6  << 21;
    carry8  = (s8  + (1 << 20)) >> 21; s9  += carry8;  s8  -= carry8  << 21;
    carry10 = (s10 + (1 << 20)) >> 21; s11 += carry10; s10 -= carry10 << 21;

    carry1  = (s1  + (1 << 20)) >> 21; s2  += carry1;  s1  -= carry1  << 21;
    carry3  = (s3  + (1 << 20)) >> 21; s4  += carry3;  s3  -= carry3  << 21;
    carry5  = (s5  + (1 << 20)) >> 21; s6  += carry5;  s5  -= carry5  << 21;
    carry7  = (s7  + (1 << 20)) >> 21; s8  += carry7;  s7  -= carry7  << 21;
    carry9  = (s9  + (1 << 20)) >> 21; s10 += carry9;  s9  -= carry9  << 21;
    carry11 = (s11 + (1 << 20)) >> 21; s12 += carry11; s11 -= carry11 << 21;

    /* Reduce s12 overflow back into s0..s5 */
    s0  += s12 * 666643; s1  += s12 * 470296;
    s2  += s12 * 654183; s3  -= s12 * 997805;
    s4  += s12 * 136657; s5  -= s12 * 683901; s12 = 0;

    /* Carry-propagate again (even then odd, with unsigned carry for last pass) */
    carry0  = (s0  + (1 << 20)) >> 21; s1  += carry0;  s0  -= carry0  << 21;
    carry2  = (s2  + (1 << 20)) >> 21; s3  += carry2;  s2  -= carry2  << 21;
    carry4  = (s4  + (1 << 20)) >> 21; s5  += carry4;  s4  -= carry4  << 21;
    carry6  = (s6  + (1 << 20)) >> 21; s7  += carry6;  s6  -= carry6  << 21;
    carry8  = (s8  + (1 << 20)) >> 21; s9  += carry8;  s8  -= carry8  << 21;
    carry10 = (s10 + (1 << 20)) >> 21; s11 += carry10; s10 -= carry10 << 21;

    carry1 = s1  >> 21; s2  += carry1; s1  -= carry1 << 21;
    carry3 = s3  >> 21; s4  += carry3; s3  -= carry3 << 21;
    carry5 = s5  >> 21; s6  += carry5; s5  -= carry5 << 21;
    carry7 = s7  >> 21; s8  += carry7; s7  -= carry7 << 21;
    carry9 = s9  >> 21; s10 += carry9; s9  -= carry9 << 21;
    carry11 = s11 >> 21; s12 += carry11; s11 -= carry11 << 21;

    s0  += s12 * 666643; s1  += s12 * 470296;
    s2  += s12 * 654183; s3  -= s12 * 997805;
    s4  += s12 * 136657; s5  -= s12 * 683901; s12 = 0;

    carry0 = s0  >> 21; s1  += carry0; s0  -= carry0 << 21;
    carry1 = s1  >> 21; s2  += carry1; s1  -= carry1 << 21;
    carry2 = s2  >> 21; s3  += carry2; s2  -= carry2 << 21;
    carry3 = s3  >> 21; s4  += carry3; s3  -= carry3 << 21;
    carry4 = s4  >> 21; s5  += carry4; s4  -= carry4 << 21;
    carry5 = s5  >> 21; s6  += carry5; s5  -= carry5 << 21;
    carry6 = s6  >> 21; s7  += carry6; s6  -= carry6 << 21;
    carry7 = s7  >> 21; s8  += carry7; s7  -= carry7 << 21;
    carry8 = s8  >> 21; s9  += carry8; s8  -= carry8 << 21;
    carry9 = s9  >> 21; s10 += carry9; s9  -= carry9 << 21;
    carry10 = s10 >> 21; s11 += carry10; s10 -= carry10 << 21;

    /* Pack 12 x 21-bit limbs into 32 bytes, little-endian */
    out[ 0] = (uint8_t)(s0);
    out[ 1] = (uint8_t)(s0 >>  8);
    out[ 2] = (uint8_t)((s0 >> 16) | (s1 <<  5));
    out[ 3] = (uint8_t)(s1 >>  3);
    out[ 4] = (uint8_t)(s1 >> 11);
    out[ 5] = (uint8_t)((s1 >> 19) | (s2 <<  2));
    out[ 6] = (uint8_t)(s2 >>  6);
    out[ 7] = (uint8_t)((s2 >> 14) | (s3 <<  7));
    out[ 8] = (uint8_t)(s3 >>  1);
    out[ 9] = (uint8_t)(s3 >>  9);
    out[10] = (uint8_t)((s3 >> 17) | (s4 <<  4));
    out[11] = (uint8_t)(s4 >>  4);
    out[12] = (uint8_t)(s4 >> 12);
    out[13] = (uint8_t)((s4 >> 20) | (s5 <<  1));
    out[14] = (uint8_t)(s5 >>  7);
    out[15] = (uint8_t)((s5 >> 15) | (s6 <<  6));
    out[16] = (uint8_t)(s6 >>  2);
    out[17] = (uint8_t)(s6 >> 10);
    out[18] = (uint8_t)((s6 >> 18) | (s7 <<  3));
    out[19] = (uint8_t)(s7 >>  5);
    out[20] = (uint8_t)(s7 >> 13);
    out[21] = (uint8_t)(s8);
    out[22] = (uint8_t)(s8 >>  8);
    out[23] = (uint8_t)((s8 >> 16) | (s9 <<  5));
    out[24] = (uint8_t)(s9 >>  3);
    out[25] = (uint8_t)(s9 >> 11);
    out[26] = (uint8_t)((s9 >> 19) | (s10 <<  2));
    out[27] = (uint8_t)(s10 >>  6);
    out[28] = (uint8_t)((s10 >> 14) | (s11 <<  7));
    out[29] = (uint8_t)(s11 >>  1);
    out[30] = (uint8_t)(s11 >>  9);
    out[31] = (uint8_t)(s11 >> 17);
}

/* ── Double-scalar multiplication: [a]B + [b]P ── */

/* This computes the verification equation [s]B - [h]A
 * using a simple double-scalar multiplication.
 *
 * We use a basic double-and-add approach for both scalars simultaneously.
 * For production, a windowed method or Straus' algorithm would be faster,
 * but this is correct and compact.
 */

/* Simple scalar multiplication: [scalar]P */
static void ge_scalarmult(ge_p3 *r, const uint8_t scalar[32], const ge_p3 *P)
{
    ge_p3 Q;
    ge_p3_0(&Q);  /* Q = identity */

    ge_cached Pcached;
    ge_p3_to_cached(&Pcached, P);

    /* Process bits from high to low */
    for (int i = 255; i >= 0; i--) {
        /* Double */
        ge_p2 Q2;
        ge_p3_to_p2(&Q2, &Q);
        ge_p1p1 Q1p1;
        ge_p2_dbl(&Q1p1, &Q2);
        ge_p1p1_to_p3(&Q, &Q1p1);

        /* Add if bit is set */
        int byte_idx = i / 8;
        int bit_idx = i % 8;
        int bit = (scalar[byte_idx] >> bit_idx) & 1;
        if (bit) {
            ge_p1p1 sum;
            ge_add(&sum, &Q, &Pcached);
            ge_p1p1_to_p3(&Q, &sum);
            /* Re-cache P since Q changed but P didn't */
        }
    }

    /* Copy result */
    fe_copy(r->X, Q.X);
    fe_copy(r->Y, Q.Y);
    fe_copy(r->Z, Q.Z);
    fe_copy(r->T, Q.T);
}

/* Double-scalar multiplication: [a]B + [b]P
 * Uses Shamir's trick: process both scalars simultaneously. */
static void ge_double_scalarmult(ge_p3 *r,
                                  const uint8_t a[32], /* scalar for B */
                                  const ge_p3 *B,
                                  const uint8_t b[32], /* scalar for P */
                                  const ge_p3 *P)
{
    ge_p3 Q;
    ge_p3_0(&Q);  /* Q = identity */

    ge_cached Bcached, Pcached;
    ge_p3_to_cached(&Bcached, B);
    ge_p3_to_cached(&Pcached, P);

    /* Pre-compute B+P for Shamir's trick */
    ge_p3 BP;
    {
        ge_p1p1 tmp;
        ge_add(&tmp, B, &Pcached);
        ge_p1p1_to_p3(&BP, &tmp);
    }
    ge_cached BPcached;
    ge_p3_to_cached(&BPcached, &BP);

    /* Process bits from high to low */
    for (int i = 255; i >= 0; i--) {
        /* Double */
        ge_p2 Q2;
        ge_p3_to_p2(&Q2, &Q);
        ge_p1p1 Q1p1;
        ge_p2_dbl(&Q1p1, &Q2);
        ge_p1p1_to_p3(&Q, &Q1p1);

        int byte_idx = i / 8;
        int bit_idx = i % 8;
        int bit_a = (a[byte_idx] >> bit_idx) & 1;
        int bit_b = (b[byte_idx] >> bit_idx) & 1;

        if (bit_a && bit_b) {
            /* Add B + P */
            ge_p1p1 sum;
            ge_add(&sum, &Q, &BPcached);
            ge_p1p1_to_p3(&Q, &sum);
        } else if (bit_a) {
            /* Add B */
            ge_p1p1 sum;
            ge_add(&sum, &Q, &Bcached);
            ge_p1p1_to_p3(&Q, &sum);
        } else if (bit_b) {
            /* Add P */
            ge_p1p1 sum;
            ge_add(&sum, &Q, &Pcached);
            ge_p1p1_to_p3(&Q, &sum);
        }
    }

    fe_copy(r->X, Q.X);
    fe_copy(r->Y, Q.Y);
    fe_copy(r->Z, Q.Z);
    fe_copy(r->T, Q.T);
}

/* ── Constant-time comparison ── */
static int ct_memcmp(const uint8_t *a, const uint8_t *b, int len)
{
    uint8_t diff = 0;
    for (int i = 0; i < len; i++)
        diff |= a[i] ^ b[i];
    return diff;
}

/* Check if a 32-byte scalar is less than L (the group order) */
static int sc_is_canonical(const uint8_t s[32])
{
    /* s must be < L. Compare from high byte to low. */
    for (int i = 31; i >= 0; i--) {
        if (s[i] < ed25519_L[i]) return 1;  /* s < L, canonical */
        if (s[i] > ed25519_L[i]) return 0;  /* s > L, not canonical */
    }
    return 0; /* s == L, not canonical (must be strictly less) */
}

/* ── Ed25519 signature verification (RFC 8032) ──
 *
 * Signature = (R, S) where R is a curve point (32 bytes) and S is a scalar (32 bytes)
 * Public key A is a curve point (32 bytes)
 *
 * Verification:
 *   1. Decode R and A as curve points
 *   2. h = SHA-512(R || A || message) mod L
 *   3. Check: [S]B == R + [h]A
 *      Equivalently: [S]B - [h]A == R
 *
 * We check [S]B == R + [h]A by computing both sides and comparing.
 */
static int ed25519_verify(const uint8_t public_key[32],
                           const uint8_t *message, uint64_t msg_len,
                           const uint8_t signature[64])
{
    const uint8_t *R_bytes = signature;       /* First 32 bytes */
    const uint8_t *S_bytes = signature + 32;  /* Last 32 bytes */

    /* 1. Check S < L (reject non-canonical S) */
    if (!sc_is_canonical(S_bytes))
        return -1;

    /* 2. Decode public key A */
    ge_p3 A;
    if (ge_frombytes(&A, public_key) != 0)
        return -2;

    /* 3. Decode R */
    ge_p3 R_point;
    if (ge_frombytes(&R_point, R_bytes) != 0)
        return -3;

    /* 4. Compute h = SHA-512(R || A || message) */
    sha512_ctx_t sha_ctx;
    uint8_t h_full[64];
    sha512_init(&sha_ctx);
    sha512_update(&sha_ctx, R_bytes, 32);
    sha512_update(&sha_ctx, public_key, 32);
    sha512_update(&sha_ctx, message, msg_len);
    sha512_final(&sha_ctx, h_full);

    /* 5. Reduce h mod L */
    uint8_t h[32];
    sc_reduce(h, h_full);

    /* 6. Compute [S]B (base point multiplication) */
    ge_p3 B;
    fe_frombytes(B.X, ed25519_Bx_bytes);
    fe_frombytes(B.Y, ed25519_By_bytes);
    fe_1(B.Z);
    fe_mul(B.T, B.X, B.Y);

    /* 7. Compute R_check = [S]B - [h]A
     * Which is [S]B + [-h]A = [S]B + [L-h]A
     *
     * Alternative: compute [S]B and R + [h]A separately and compare.
     * Let's compute lhs = [S]B and rhs = R + [h]A, then compare. */

    /* Compute [h]A */
    ge_p3 hA;
    ge_scalarmult(&hA, h, &A);

    /* Compute rhs = R + [h]A */
    ge_p3 rhs;
    {
        ge_cached hA_cached;
        ge_p3_to_cached(&hA_cached, &hA);
        ge_p1p1 tmp;
        ge_add(&tmp, &R_point, &hA_cached);
        ge_p1p1_to_p3(&rhs, &tmp);
    }

    /* Compute lhs = [S]B */
    ge_p3 lhs;
    ge_scalarmult(&lhs, S_bytes, &B);

    /* 8. Compare lhs and rhs by encoding both to bytes */
    uint8_t lhs_bytes[32], rhs_bytes[32];
    ge_tobytes(lhs_bytes, &lhs);
    ge_tobytes(rhs_bytes, &rhs);

    if (ct_memcmp(lhs_bytes, rhs_bytes, 32) != 0)
        return -4;

    return 0;  /* Signature valid */
}

/* ── Public API ── */

hal_status_t ajdrv_verify_signature(const uint8_t pub_key[AJDRV_PUBKEY_SIZE],
                                     const void *data, uint64_t size)
{
    if (size < sizeof(ajdrv_header_t) + AJDRV_SIGNATURE_SIZE)
        return HAL_ERROR;

    const ajdrv_header_t *hdr = (const ajdrv_header_t *)data;

    /* Validate header */
    int rc = ajdrv_validate_header(hdr, size);
    if (rc != 0) {
        hal_console_printf("[verify] Invalid header: %d\n", rc);
        return HAL_ERROR;
    }

    /* The signed region is [0..signature_offset) */
    uint32_t signed_len = hdr->signature_offset;
    const uint8_t *signature = (const uint8_t *)data + hdr->signature_offset;

    /* Verify Ed25519 signature over the signed region */
    rc = ed25519_verify(pub_key,
                        (const uint8_t *)data, signed_len,
                        signature);

    if (rc != 0) {
        hal_console_printf("[verify] Ed25519 verification failed: %d\n", rc);
        return HAL_ERROR;
    }

    hal_console_puts("[verify] Ed25519 signature valid\n");
    return HAL_OK;
}

hal_status_t ajdrv_verify(const void *data, uint64_t size)
{
    if (!g_key_set)
        return HAL_NOT_SUPPORTED;  /* No key configured — skip verification */
    return ajdrv_verify_signature(g_trusted_key, data, size);
}
