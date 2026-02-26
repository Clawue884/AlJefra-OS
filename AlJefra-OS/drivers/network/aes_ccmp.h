/* SPDX-License-Identifier: MIT */
/* AlJefra OS -- AES-128 CCMP (Counter with CBC-MAC) for WPA2
 * Freestanding implementation -- no libc dependencies.
 *
 * Implements:
 *   - AES-128 block cipher (encrypt only; CCMP uses AES-CTR + AES-CBC-MAC)
 *   - CCM mode per IEEE 802.11i / RFC 3610
 *   - CCMP encrypt/decrypt for 802.11 data frames
 */

#ifndef ALJEFRA_AES_CCMP_H
#define ALJEFRA_AES_CCMP_H

#include "../../hal/hal.h"

/* AES-128 block/key sizes */
#define AES_BLOCK_SIZE   16
#define AES_KEY_SIZE     16
#define AES_NUM_ROUNDS   10

/* CCMP parameters per 802.11i */
#define CCMP_MIC_LEN     8    /* Message Integrity Code length */
#define CCMP_HDR_LEN     8    /* CCMP header (IV) length */
#define CCMP_PN_LEN      6    /* Packet Number length (48-bit) */
#define CCMP_NONCE_LEN   13   /* Nonce length for CCM */

/* AES-128 expanded key schedule (11 round keys * 4 words) */
typedef struct {
    uint32_t rk[44];          /* Round keys (4 * (Nr+1) = 44 words) */
} aes128_ctx_t;

/* ---- AES-128 Core ---- */

/* Initialize AES-128 key schedule from a 16-byte key */
void aes128_init(aes128_ctx_t *ctx, const uint8_t key[AES_KEY_SIZE]);

/* Encrypt a single 16-byte block in place */
void aes128_encrypt_block(const aes128_ctx_t *ctx,
                           const uint8_t in[AES_BLOCK_SIZE],
                           uint8_t out[AES_BLOCK_SIZE]);

/* ---- CCM Mode ---- */

/* CCM-encrypt (generates ciphertext + MIC).
 *
 * nonce:      13-byte nonce (constructed from A2, PN, priority)
 * aad:        Additional Authenticated Data (802.11 header fields)
 * aad_len:    Length of AAD
 * plaintext:  Data to encrypt
 * pt_len:     Length of plaintext
 * ciphertext: Output buffer (must be at least pt_len + CCMP_MIC_LEN bytes)
 *
 * Returns HAL_OK on success.
 */
hal_status_t ccm_encrypt(const aes128_ctx_t *ctx,
                          const uint8_t nonce[CCMP_NONCE_LEN],
                          const uint8_t *aad, uint32_t aad_len,
                          const uint8_t *plaintext, uint32_t pt_len,
                          uint8_t *ciphertext);

/* CCM-decrypt and verify MIC.
 *
 * ciphertext: Encrypted data + MIC (length = ct_len)
 * ct_len:     Length of ciphertext including MIC (must be >= CCMP_MIC_LEN)
 * plaintext:  Output buffer (must be at least ct_len - CCMP_MIC_LEN bytes)
 *
 * Returns HAL_OK if MIC verifies, HAL_ERROR if authentication fails.
 */
hal_status_t ccm_decrypt(const aes128_ctx_t *ctx,
                          const uint8_t nonce[CCMP_NONCE_LEN],
                          const uint8_t *aad, uint32_t aad_len,
                          const uint8_t *ciphertext, uint32_t ct_len,
                          uint8_t *plaintext);

/* ---- CCMP 802.11 Wrappers ---- */

/* Build a CCMP nonce from 802.11 frame fields.
 *
 * priority:   QoS TID (0 for non-QoS)
 * addr2:      Transmitter address (6 bytes)
 * pn:         48-bit packet number (6 bytes, little-endian)
 * nonce:      Output 13-byte nonce
 */
void ccmp_build_nonce(uint8_t priority, const uint8_t addr2[6],
                       const uint8_t pn[CCMP_PN_LEN],
                       uint8_t nonce[CCMP_NONCE_LEN]);

/* Build the CCMP AAD from an 802.11 header.
 *
 * hdr:      Pointer to 802.11 MAC header
 * hdr_len:  Length of MAC header (24 or 30 bytes for QoS, 26 for addr4)
 * aad:      Output AAD buffer (must be at least 30 bytes)
 *
 * Returns the AAD length.
 */
uint32_t ccmp_build_aad(const uint8_t *hdr, uint32_t hdr_len,
                          uint8_t *aad);

/* Build the 8-byte CCMP header (inserted after MAC header).
 *
 * pn:       48-bit packet number (6 bytes)
 * key_id:   Key ID (0-3, usually 0)
 * out:      Output 8-byte CCMP header
 */
void ccmp_build_hdr(const uint8_t pn[CCMP_PN_LEN], uint8_t key_id,
                     uint8_t out[CCMP_HDR_LEN]);

/* Parse a CCMP header to extract PN and key ID.
 *
 * hdr:      8-byte CCMP header
 * pn:       Output 6-byte PN
 * key_id:   Output key ID
 */
void ccmp_parse_hdr(const uint8_t hdr[CCMP_HDR_LEN],
                     uint8_t pn[CCMP_PN_LEN], uint8_t *key_id);

/* Full CCMP encryption of an 802.11 data frame.
 *
 * tk:          Temporal Key (16 bytes, from 4-way handshake)
 * frame:       Complete 802.11 frame (header + plaintext payload)
 * frame_len:   Length of input frame
 * pn:          48-bit packet number for this frame
 * out:         Output buffer (header + CCMP hdr + encrypted payload + MIC)
 *              Must be at least frame_len + CCMP_HDR_LEN + CCMP_MIC_LEN
 * out_len:     Set to actual output length
 *
 * Returns HAL_OK on success.
 */
hal_status_t ccmp_encrypt_frame(const uint8_t tk[AES_KEY_SIZE],
                                 const uint8_t *frame, uint32_t frame_len,
                                 const uint8_t pn[CCMP_PN_LEN],
                                 uint8_t *out, uint32_t *out_len);

/* Full CCMP decryption and MIC verification of an 802.11 data frame.
 *
 * tk:          Temporal Key (16 bytes)
 * frame:       Complete 802.11 frame (header + CCMP hdr + ciphertext + MIC)
 * frame_len:   Length of input frame
 * out:         Output buffer (header + decrypted payload)
 *              Must be at least frame_len - CCMP_HDR_LEN - CCMP_MIC_LEN
 * out_len:     Set to actual output length
 *
 * Returns HAL_OK if MIC verifies, HAL_ERROR on auth failure.
 */
hal_status_t ccmp_decrypt_frame(const uint8_t tk[AES_KEY_SIZE],
                                 const uint8_t *frame, uint32_t frame_len,
                                 uint8_t *out, uint32_t *out_len);

#endif /* ALJEFRA_AES_CCMP_H */
