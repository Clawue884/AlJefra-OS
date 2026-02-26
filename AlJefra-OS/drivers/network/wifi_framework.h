/* SPDX-License-Identifier: MIT */
/* AlJefra OS -- 802.11 WiFi Framework
 * Architecture-independent WiFi stack providing:
 *   - 802.11 frame parsing (management, control, data)
 *   - WPA2-PSK authentication (4-way handshake)
 *   - EAPOL key frame handling
 *   - BSS scanning (probe request/response, beacon parsing)
 *   - Association/authentication state machine
 *   - Data frame encapsulation (802.11 <-> Ethernet conversion)
 *
 * Uses HAL for all hardware access. Crypto provided by aes_ccmp module.
 */

#ifndef ALJEFRA_WIFI_FRAMEWORK_H
#define ALJEFRA_WIFI_FRAMEWORK_H

#include "../../hal/hal.h"
#include "aes_ccmp.h"

/* ====================================================================
 * 802.11 Constants
 * ==================================================================== */

/* Frame type (FC bits 2-3) */
#define WIFI_TYPE_MGMT       0
#define WIFI_TYPE_CTRL       1
#define WIFI_TYPE_DATA       2

/* Management subtypes (FC bits 4-7) */
#define WIFI_MGMT_ASSOC_REQ    0x0
#define WIFI_MGMT_ASSOC_RESP   0x1
#define WIFI_MGMT_REASSOC_REQ  0x2
#define WIFI_MGMT_REASSOC_RESP 0x3
#define WIFI_MGMT_PROBE_REQ    0x4
#define WIFI_MGMT_PROBE_RESP   0x5
#define WIFI_MGMT_BEACON       0x8
#define WIFI_MGMT_DISASSOC     0xA
#define WIFI_MGMT_AUTH         0xB
#define WIFI_MGMT_DEAUTH       0xC
#define WIFI_MGMT_ACTION       0xD

/* Data subtypes */
#define WIFI_DATA_DATA         0x0
#define WIFI_DATA_NULL         0x4
#define WIFI_DATA_QOS          0x8
#define WIFI_DATA_QOS_NULL     0xC

/* Control subtypes */
#define WIFI_CTRL_ACK          0xD
#define WIFI_CTRL_RTS          0xB
#define WIFI_CTRL_CTS          0xC

/* Frame Control flags (byte 1) */
#define WIFI_FC_TODS           0x01
#define WIFI_FC_FROMDS         0x02
#define WIFI_FC_MOREFRAG       0x04
#define WIFI_FC_RETRY          0x08
#define WIFI_FC_PWRMGT         0x10
#define WIFI_FC_MOREDATA       0x20
#define WIFI_FC_PROTECTED      0x40
#define WIFI_FC_ORDER          0x80

/* Authentication algorithms */
#define WIFI_AUTH_OPEN         0
#define WIFI_AUTH_SHARED_KEY   1

/* Status codes */
#define WIFI_STATUS_SUCCESS    0

/* Reason codes */
#define WIFI_REASON_UNSPECIFIED  1

/* Information Element IDs */
#define WIFI_IE_SSID           0
#define WIFI_IE_SUPPORTED_RATES 1
#define WIFI_IE_DS_PARAMS      3
#define WIFI_IE_TIM            5
#define WIFI_IE_COUNTRY        7
#define WIFI_IE_RSN            48
#define WIFI_IE_EXT_RATES      50
#define WIFI_IE_HT_CAP         45
#define WIFI_IE_HT_OP          61
#define WIFI_IE_VHT_CAP        191
#define WIFI_IE_VHT_OP         192
#define WIFI_IE_VENDOR         221

/* RSN / WPA2 OUI and suite types */
#define RSN_OUI_TYPE           0x000FAC  /* 00-0F-AC */
#define RSN_CIPHER_CCMP        4
#define RSN_AKM_PSK            2

/* Maximum sizes */
#define WIFI_MAX_SSID_LEN      32
#define WIFI_MAX_FRAME_LEN     2346
#define WIFI_MAX_IE_LEN        255
#define WIFI_MAX_SCAN_RESULTS  16
#define WIFI_ETHER_ADDR_LEN    6

/* Ethernet header size (for 802.11 <-> Ethernet conversion) */
#define ETHER_HDR_LEN          14
/* LLC/SNAP header */
#define LLC_SNAP_HDR_LEN       8

/* ====================================================================
 * 802.11 Frame Structures
 * ==================================================================== */

/* Generic 802.11 MAC header (24 bytes base) */
typedef struct __attribute__((packed)) {
    uint16_t frame_control;
    uint16_t duration;
    uint8_t  addr1[6];          /* Receiver / DA */
    uint8_t  addr2[6];          /* Transmitter / SA */
    uint8_t  addr3[6];          /* BSSID or other */
    uint16_t seq_ctrl;
} wifi_mac_hdr_t;

/* QoS MAC header (26 bytes) */
typedef struct __attribute__((packed)) {
    wifi_mac_hdr_t hdr;
    uint16_t qos_ctrl;
} wifi_qos_hdr_t;

/* Beacon / Probe Response fixed fields (12 bytes) */
typedef struct __attribute__((packed)) {
    uint64_t timestamp;
    uint16_t beacon_interval;
    uint16_t capability;
} wifi_beacon_fixed_t;

/* Authentication frame fixed fields (6 bytes) */
typedef struct __attribute__((packed)) {
    uint16_t auth_algorithm;
    uint16_t auth_seq;
    uint16_t status_code;
} wifi_auth_fixed_t;

/* Association Request fixed fields (4 bytes) */
typedef struct __attribute__((packed)) {
    uint16_t capability;
    uint16_t listen_interval;
} wifi_assoc_req_fixed_t;

/* Association Response fixed fields (6 bytes) */
typedef struct __attribute__((packed)) {
    uint16_t capability;
    uint16_t status_code;
    uint16_t assoc_id;
} wifi_assoc_resp_fixed_t;

/* LLC/SNAP header for data frames */
typedef struct __attribute__((packed)) {
    uint8_t  dsap;              /* 0xAA */
    uint8_t  ssap;              /* 0xAA */
    uint8_t  ctrl;              /* 0x03 */
    uint8_t  oui[3];            /* 0x00, 0x00, 0x00 */
    uint16_t ether_type;        /* Big-endian */
} wifi_llc_snap_t;

/* ====================================================================
 * EAPOL / WPA2 4-Way Handshake
 * ==================================================================== */

/* EAPOL frame types */
#define EAPOL_TYPE_KEY         3
#define EAPOL_ETHER_TYPE       0x888E   /* Big-endian on wire */

/* EAPOL-Key descriptor types */
#define EAPOL_KEY_DESC_RSN     2

/* EAPOL-Key Info bits */
#define EAPOL_KEY_INFO_TYPE_MASK   0x0007
#define EAPOL_KEY_INFO_INSTALL     0x0040
#define EAPOL_KEY_INFO_ACK         0x0080
#define EAPOL_KEY_INFO_MIC         0x0100
#define EAPOL_KEY_INFO_SECURE      0x0200
#define EAPOL_KEY_INFO_ERROR       0x0400
#define EAPOL_KEY_INFO_REQUEST     0x0800
#define EAPOL_KEY_INFO_ENC_KEY     0x1000
#define EAPOL_KEY_INFO_PAIRWISE    0x0008

/* EAPOL header (4 bytes) */
typedef struct __attribute__((packed)) {
    uint8_t  version;           /* 1 or 2 */
    uint8_t  type;              /* 3 = Key */
    uint16_t length;            /* Big-endian body length */
} eapol_hdr_t;

/* EAPOL-Key frame body */
typedef struct __attribute__((packed)) {
    uint8_t  descriptor_type;   /* 2 = RSN */
    uint16_t key_info;          /* Big-endian */
    uint16_t key_length;        /* Big-endian */
    uint8_t  replay_counter[8];
    uint8_t  key_nonce[32];
    uint8_t  key_iv[16];
    uint8_t  key_rsc[8];
    uint8_t  reserved[8];
    uint8_t  key_mic[16];
    uint16_t key_data_length;   /* Big-endian */
    /* Key data follows */
} eapol_key_t;

/* WPA2-PSK key hierarchy */
#define PMK_LEN                32
#define PTK_LEN                48   /* KCK(16) + KEK(16) + TK(16) for CCMP */
#define KCK_LEN                16   /* Key Confirmation Key */
#define KEK_LEN                16   /* Key Encryption Key */
#define TK_LEN                 16   /* Temporal Key (for CCMP) */
#define GTK_LEN                16   /* Group Temporal Key */
#define NONCE_LEN              32   /* ANonce / SNonce */

/* PTK component offsets */
#define PTK_KCK_OFFSET         0
#define PTK_KEK_OFFSET         16
#define PTK_TK_OFFSET          32

/* ====================================================================
 * WiFi State Machine
 * ==================================================================== */

typedef enum {
    WIFI_STATE_IDLE = 0,        /* Not connected, not scanning */
    WIFI_STATE_SCANNING,        /* Active/passive scan in progress */
    WIFI_STATE_AUTHENTICATING,  /* Sending/receiving auth frames */
    WIFI_STATE_ASSOCIATING,     /* Sending/receiving assoc frames */
    WIFI_STATE_4WAY_1,          /* Received message 1 of 4-way */
    WIFI_STATE_4WAY_2,          /* Sent message 2, waiting for 3 */
    WIFI_STATE_4WAY_3,          /* Received message 3, sent 4 */
    WIFI_STATE_CONNECTED,       /* Fully associated + keys installed */
    WIFI_STATE_DISCONNECTING,   /* Deauth/disassoc in progress */
} wifi_state_t;

/* BSS (network) descriptor from scan */
typedef struct {
    uint8_t  bssid[6];
    uint8_t  ssid[WIFI_MAX_SSID_LEN + 1];  /* Null-terminated */
    uint8_t  ssid_len;
    uint8_t  channel;
    int8_t   rssi;                          /* Signal strength (dBm) */
    uint16_t beacon_interval;
    uint16_t capability;
    bool     wpa2;                          /* Supports WPA2-PSK */
    bool     has_rsn;                       /* Has RSN IE */
} wifi_bss_t;

/* ====================================================================
 * WiFi Driver Callbacks (implemented by hardware driver)
 * ==================================================================== */

typedef struct {
    /* Send a raw 802.11 frame (including MAC header).
     * Returns HAL_OK on success. */
    hal_status_t (*tx_raw)(const void *frame, uint32_t len);

    /* Receive a raw 802.11 frame (polling).
     * buf must be at least WIFI_MAX_FRAME_LEN bytes.
     * Returns HAL_OK if frame received; sets *len. */
    hal_status_t (*rx_raw)(void *buf, uint32_t *len);

    /* Set the radio channel (1-14 for 2.4GHz, higher for 5GHz) */
    hal_status_t (*set_channel)(uint8_t channel);

    /* Get this device's MAC address */
    void (*get_mac)(uint8_t mac[6]);

    /* Enable/disable promiscuous mode (for scanning) */
    hal_status_t (*set_promisc)(bool enable);
} wifi_hw_ops_t;

/* ====================================================================
 * WiFi Context (per-connection state)
 * ==================================================================== */

typedef struct {
    /* Hardware callbacks */
    const wifi_hw_ops_t *hw;

    /* State */
    wifi_state_t state;

    /* Our MAC address */
    uint8_t      our_mac[6];

    /* Current BSS */
    wifi_bss_t   bss;
    uint16_t     assoc_id;

    /* WPA2 key material */
    uint8_t      pmk[PMK_LEN];       /* Pre-shared Master Key (from passphrase) */
    uint8_t      ptk[PTK_LEN];       /* Pairwise Transient Key */
    uint8_t      gtk[GTK_LEN];       /* Group Temporal Key */
    uint8_t      anonce[NONCE_LEN];  /* AP nonce */
    uint8_t      snonce[NONCE_LEN];  /* Our nonce */
    uint8_t      replay_counter[8];  /* EAPOL replay counter */
    bool         ptk_installed;
    bool         gtk_installed;

    /* TX packet number (48-bit, PN for CCMP) */
    uint8_t      tx_pn[CCMP_PN_LEN];

    /* RX packet number (for replay detection) */
    uint8_t      rx_pn[CCMP_PN_LEN];

    /* Sequence number for TX frames */
    uint16_t     tx_seq;

    /* Scan results */
    wifi_bss_t   scan_results[WIFI_MAX_SCAN_RESULTS];
    uint32_t     scan_count;

    /* Temporary frame buffer */
    uint8_t      frame_buf[WIFI_MAX_FRAME_LEN + 64];
} wifi_ctx_t;

/* ====================================================================
 * Public API
 * ==================================================================== */

/* Initialize the WiFi framework context.
 * hw:  Hardware operations table (provided by the WiFi driver)
 */
hal_status_t wifi_init(wifi_ctx_t *ctx, const wifi_hw_ops_t *hw);

/* Scan for available networks.
 * Performs an active scan (sends probe requests) across channels.
 * Results are stored in ctx->scan_results.
 *
 * channels:  Array of channel numbers to scan (NULL = all 2.4GHz channels)
 * n_chan:    Number of channels (0 = default 1-11)
 * dwell_ms: Time to listen on each channel (ms, 0 = default 100ms)
 *
 * Returns number of BSS entries found.
 */
uint32_t wifi_scan(wifi_ctx_t *ctx, const uint8_t *channels,
                    uint32_t n_chan, uint32_t dwell_ms);

/* Connect to a WPA2-PSK network.
 *
 * ssid:       SSID to connect to (null-terminated)
 * passphrase: WPA2 passphrase (8-63 characters, null-terminated)
 *
 * This performs the full connection sequence:
 *   1. Scan for the network (if not already in scan results)
 *   2. Open System Authentication
 *   3. Association
 *   4. WPA2 4-way handshake
 *
 * Returns HAL_OK when fully connected with keys installed.
 */
hal_status_t wifi_connect(wifi_ctx_t *ctx, const char *ssid,
                           const char *passphrase);

/* Disconnect from the current network. */
hal_status_t wifi_disconnect(wifi_ctx_t *ctx);

/* Send an Ethernet frame over WiFi.
 * The framework handles:
 *   - Ethernet -> 802.11 conversion
 *   - LLC/SNAP encapsulation
 *   - CCMP encryption (if connected with WPA2)
 *
 * frame:  Ethernet frame (14-byte header + payload)
 * len:    Total frame length
 */
hal_status_t wifi_send(wifi_ctx_t *ctx, const void *frame, uint32_t len);

/* Receive an Ethernet frame from WiFi.
 * The framework handles:
 *   - 802.11 -> Ethernet conversion
 *   - CCMP decryption and MIC verification
 *   - LLC/SNAP de-encapsulation
 *
 * buf:    Output buffer (must be at least 1514 bytes)
 * len:    Set to received frame length
 *
 * Returns HAL_OK if a frame was received, HAL_NO_DEVICE if none pending.
 */
hal_status_t wifi_recv(wifi_ctx_t *ctx, void *buf, uint32_t *len);

/* Process a received 802.11 frame (management, EAPOL, data).
 * Called internally or by the driver when a frame arrives.
 * Updates state machine as needed.
 */
hal_status_t wifi_process_frame(wifi_ctx_t *ctx, const void *frame,
                                 uint32_t len);

/* Get current connection state */
wifi_state_t wifi_get_state(wifi_ctx_t *ctx);

/* Check if connected and keys installed */
bool wifi_is_connected(wifi_ctx_t *ctx);

/* ---- Frame Construction Helpers ---- */

/* Build a probe request frame.
 * ssid can be NULL for wildcard (broadcast) probe.
 */
uint32_t wifi_build_probe_req(wifi_ctx_t *ctx, const char *ssid,
                                uint8_t *out);

/* Build an authentication frame (Open System, seq 1) */
uint32_t wifi_build_auth(wifi_ctx_t *ctx, uint8_t *out);

/* Build an association request frame */
uint32_t wifi_build_assoc_req(wifi_ctx_t *ctx, uint8_t *out);

/* Build a deauthentication frame */
uint32_t wifi_build_deauth(wifi_ctx_t *ctx, uint16_t reason, uint8_t *out);

/* Build an EAPOL-Key message 2 (response to AP's message 1) */
uint32_t wifi_build_eapol_msg2(wifi_ctx_t *ctx, uint8_t *out);

/* Build an EAPOL-Key message 4 (response to AP's message 3) */
uint32_t wifi_build_eapol_msg4(wifi_ctx_t *ctx, uint8_t *out);

/* ---- 802.11 <-> Ethernet Conversion ---- */

/* Convert an 802.11 data frame to Ethernet.
 * Strips MAC header, removes LLC/SNAP, builds Ethernet header.
 * Returns output length, or 0 on error.
 */
uint32_t wifi_80211_to_ether(const uint8_t *wifi_frame, uint32_t wifi_len,
                               uint8_t *ether_frame);

/* Convert an Ethernet frame to 802.11 data frame.
 * Builds MAC header (ToDS), adds LLC/SNAP.
 * Returns output length, or 0 on error.
 */
uint32_t wifi_ether_to_80211(wifi_ctx_t *ctx,
                               const uint8_t *ether_frame, uint32_t ether_len,
                               uint8_t *wifi_frame);

/* ---- Crypto Helpers ---- */

/* Derive PMK from passphrase and SSID using PBKDF2-SHA1.
 * passphrase: WPA2 passphrase (8-63 chars)
 * ssid:       Network SSID
 * ssid_len:   SSID length
 * pmk:        Output 32-byte PMK
 */
void wifi_derive_pmk(const char *passphrase, const uint8_t *ssid,
                      uint32_t ssid_len, uint8_t pmk[PMK_LEN]);

/* Derive PTK from PMK + nonces + MAC addresses.
 * Uses PRF-384 (HMAC-SHA1 based).
 */
void wifi_derive_ptk(const uint8_t pmk[PMK_LEN],
                      const uint8_t anonce[NONCE_LEN],
                      const uint8_t snonce[NONCE_LEN],
                      const uint8_t aa[6],   /* Authenticator (AP) address */
                      const uint8_t spa[6],  /* Supplicant (our) address */
                      uint8_t ptk[PTK_LEN]);

/* Compute HMAC-SHA1 for EAPOL MIC verification */
void wifi_hmac_sha1(const uint8_t *key, uint32_t key_len,
                     const uint8_t *data, uint32_t data_len,
                     uint8_t out[20]);

#endif /* ALJEFRA_WIFI_FRAMEWORK_H */
