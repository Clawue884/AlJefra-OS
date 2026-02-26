/* SPDX-License-Identifier: MIT */
/* AlJefra OS — DHCP Client Implementation
 *
 * Implements DHCP discovery (DORA: Discover→Offer→Request→Ack).
 * Uses UDP port 68 (client) → 67 (server) via broadcast.
 *
 * Reference: RFC 2131
 */

#include "dhcp.h"
#include "checksum.h"
#include "../hal/hal.h"
#include "../kernel/driver_loader.h"
#include "../lib/string.h"
#include "../lib/endian.h"

/* ── DHCP constants ── */
#define DHCP_OP_REQUEST    1
#define DHCP_OP_REPLY      2
#define DHCP_HTYPE_ETH     1
#define DHCP_HLEN_ETH      6
#define DHCP_MAGIC_COOKIE  0x63825363

/* DHCP message types (option 53) */
#define DHCP_DISCOVER      1
#define DHCP_OFFER         2
#define DHCP_REQUEST       3
#define DHCP_DECLINE       4
#define DHCP_ACK           5
#define DHCP_NAK           6
#define DHCP_RELEASE       7

/* DHCP options */
#define OPT_SUBNET_MASK    1
#define OPT_ROUTER         3
#define OPT_DNS            6
#define OPT_HOSTNAME       12
#define OPT_REQUESTED_IP   50
#define OPT_LEASE_TIME     51
#define OPT_MSG_TYPE       53
#define OPT_SERVER_ID      54
#define OPT_PARAM_REQUEST  55
#define OPT_END            255

/* ── DHCP packet structure ── */
typedef struct __attribute__((packed)) {
    uint8_t  op;
    uint8_t  htype;
    uint8_t  hlen;
    uint8_t  hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;    /* Client IP */
    uint32_t yiaddr;    /* Your IP (from server) */
    uint32_t siaddr;    /* Server IP */
    uint32_t giaddr;    /* Gateway/relay IP */
    uint8_t  chaddr[16]; /* Client hardware address */
    uint8_t  sname[64];  /* Server hostname */
    uint8_t  file[128];  /* Boot filename */
    uint32_t cookie;     /* Magic cookie */
    uint8_t  options[312]; /* DHCP options */
} dhcp_packet_t;

/* ── Ethernet + IP + UDP headers for raw frame ── */
typedef struct __attribute__((packed)) {
    /* Ethernet */
    uint8_t  eth_dst[6];
    uint8_t  eth_src[6];
    uint16_t eth_type;
    /* IPv4 */
    uint8_t  ip_ver_ihl;
    uint8_t  ip_tos;
    uint16_t ip_len;
    uint16_t ip_id;
    uint16_t ip_frag;
    uint8_t  ip_ttl;
    uint8_t  ip_proto;
    uint16_t ip_checksum;
    uint32_t ip_src;
    uint32_t ip_dst;
    /* UDP */
    uint16_t udp_src;
    uint16_t udp_dst;
    uint16_t udp_len;
    uint16_t udp_checksum;
} net_header_t;

/* ── State ── */
static dhcp_lease_t g_lease;
static uint8_t g_mac[6];

/* ── Build DHCP discover/request ── */
static uint32_t build_dhcp_packet(dhcp_packet_t *pkt, uint8_t msg_type,
                                   uint32_t xid, uint32_t requested_ip,
                                   uint32_t server_ip)
{
    memset(pkt, 0, sizeof(*pkt));

    pkt->op = DHCP_OP_REQUEST;
    pkt->htype = DHCP_HTYPE_ETH;
    pkt->hlen = DHCP_HLEN_ETH;
    pkt->xid = htonl(xid);
    pkt->flags = htons(0x8000); /* Broadcast flag */
    pkt->cookie = htonl(DHCP_MAGIC_COOKIE);
    memcpy(pkt->chaddr, g_mac, 6);

    /* Options */
    uint8_t *opt = pkt->options;

    /* Option 53: DHCP message type */
    *opt++ = OPT_MSG_TYPE;
    *opt++ = 1;
    *opt++ = msg_type;

    if (msg_type == DHCP_REQUEST) {
        /* Option 50: Requested IP */
        *opt++ = OPT_REQUESTED_IP;
        *opt++ = 4;
        uint32_t rip = htonl(requested_ip);
        memcpy(opt, &rip, 4);
        opt += 4;

        /* Option 54: Server identifier */
        *opt++ = OPT_SERVER_ID;
        *opt++ = 4;
        uint32_t sid = htonl(server_ip);
        memcpy(opt, &sid, 4);
        opt += 4;
    }

    /* Option 55: Parameter request list */
    *opt++ = OPT_PARAM_REQUEST;
    *opt++ = 3;
    *opt++ = OPT_SUBNET_MASK;
    *opt++ = OPT_ROUTER;
    *opt++ = OPT_DNS;

    /* Option 12: Hostname */
    *opt++ = OPT_HOSTNAME;
    *opt++ = 7;
    memcpy(opt, "aljefra", 7);
    opt += 7;

    /* End */
    *opt++ = OPT_END;

    return (uint32_t)(opt - (uint8_t *)pkt);
}

/* ── Send a raw UDP frame ── */
static void send_dhcp(const dhcp_packet_t *pkt, uint32_t pkt_len)
{
    const driver_ops_t *net = driver_get_network();
    if (!net || !net->net_tx) return;

    /* Build full Ethernet + IP + UDP frame */
    uint8_t frame[1500];
    memset(frame, 0, sizeof(frame));

    net_header_t *hdr = (net_header_t *)frame;

    /* Ethernet: broadcast */
    for (int i = 0; i < 6; i++) hdr->eth_dst[i] = 0xFF;
    memcpy(hdr->eth_src, g_mac, 6);
    hdr->eth_type = htons(0x0800); /* IPv4 */

    /* IPv4 */
    uint16_t udp_total = 8 + pkt_len;
    uint16_t ip_total = 20 + udp_total;
    hdr->ip_ver_ihl = 0x45;
    hdr->ip_len = htons(ip_total);
    hdr->ip_id = htons(1);
    hdr->ip_ttl = 64;
    hdr->ip_proto = 17; /* UDP */
    hdr->ip_src = 0;
    hdr->ip_dst = 0xFFFFFFFF; /* 255.255.255.255 */
    hdr->ip_checksum = 0;
    hdr->ip_checksum = ip_checksum(&hdr->ip_ver_ihl, 20);

    /* UDP */
    hdr->udp_src = htons(68);
    hdr->udp_dst = htons(67);
    hdr->udp_len = htons(udp_total);
    hdr->udp_checksum = 0; /* Optional for IPv4 UDP */

    /* Copy DHCP payload after headers */
    memcpy(frame + sizeof(net_header_t), pkt, pkt_len);

    uint32_t total = sizeof(net_header_t) + pkt_len;
    if (total < 60) total = 60; /* Minimum Ethernet frame */

    net->net_tx(frame, total);
}

/* ── Parse DHCP options from a received packet ── */
static uint8_t parse_dhcp_options(const dhcp_packet_t *pkt, dhcp_lease_t *lease)
{
    const uint8_t *opt = pkt->options;
    uint8_t msg_type = 0;

    lease->client_ip = ntohl(pkt->yiaddr);

    for (int i = 0; i < 312 && opt[i] != OPT_END; ) {
        uint8_t code = opt[i++];
        if (code == 0) continue; /* Padding */
        uint8_t len = opt[i++];

        switch (code) {
        case OPT_MSG_TYPE:
            msg_type = opt[i];
            break;
        case OPT_SUBNET_MASK:
            if (len >= 4) {
                uint32_t v;
                memcpy(&v, &opt[i], 4);
                lease->subnet_mask = ntohl(v);
            }
            break;
        case OPT_ROUTER:
            if (len >= 4) {
                uint32_t v;
                memcpy(&v, &opt[i], 4);
                lease->gateway = ntohl(v);
            }
            break;
        case OPT_DNS:
            if (len >= 4) {
                uint32_t v;
                memcpy(&v, &opt[i], 4);
                lease->dns_server = ntohl(v);
            }
            break;
        case OPT_LEASE_TIME:
            if (len >= 4) {
                uint32_t v;
                memcpy(&v, &opt[i], 4);
                lease->lease_time = ntohl(v);
            }
            break;
        case OPT_SERVER_ID:
            if (len >= 4) {
                uint32_t v;
                memcpy(&v, &opt[i], 4);
                lease->server_ip = ntohl(v);
            }
            break;
        }
        i += len;
    }

    return msg_type;
}

/* ── Receive a DHCP reply (with timeout) ── */
static int recv_dhcp(dhcp_packet_t *pkt, uint32_t xid, uint64_t timeout_ms)
{
    const driver_ops_t *net = driver_get_network();
    if (!net || !net->net_rx) return -1;

    uint64_t start = hal_timer_ms();
    uint8_t frame[1500];

    while (hal_timer_ms() - start < timeout_ms) {
        int64_t len = net->net_rx(frame, sizeof(frame));
        if (len <= 0) {
            hal_timer_delay_us(1000); /* 1ms poll interval */
            continue;
        }

        /* Check: Ethernet type = IPv4 */
        if (len < (int64_t)sizeof(net_header_t))
            continue;
        net_header_t *hdr = (net_header_t *)frame;
        if (ntohs(hdr->eth_type) != 0x0800)
            continue;
        if (hdr->ip_proto != 17) /* UDP */
            continue;
        if (ntohs(hdr->udp_dst) != 68) /* DHCP client port */
            continue;

        /* Extract DHCP payload */
        uint32_t payload_off = sizeof(net_header_t);
        uint32_t payload_len = (uint32_t)len - payload_off;
        if (payload_len < 240) /* Minimum DHCP */
            continue;

        memcpy(pkt, frame + payload_off, payload_len);

        /* Verify XID */
        if (ntohl(pkt->xid) != xid)
            continue;
        /* Verify magic cookie */
        if (ntohl(pkt->cookie) != DHCP_MAGIC_COOKIE)
            continue;

        return 0; /* Success */
    }

    return -1; /* Timeout */
}

/* ── DHCP Discover → Offer → Request → Ack ── */

hal_status_t dhcp_discover(uint32_t *ip, uint32_t *gateway, uint32_t *dns)
{
    const driver_ops_t *net = driver_get_network();
    if (!net || !net->net_get_mac)
        return HAL_NO_DEVICE;

    net->net_get_mac(g_mac);

    /* Generate transaction ID from timer */
    uint32_t xid = (uint32_t)(hal_timer_ns() & 0xFFFFFFFF);

    dhcp_packet_t pkt;
    dhcp_lease_t lease;
    memset(&lease, 0, sizeof(lease));

    /* ── Step 1: DHCP Discover ── */
    hal_console_puts("[dhcp] Sending DISCOVER...\n");
    uint32_t pkt_len = build_dhcp_packet(&pkt, DHCP_DISCOVER, xid, 0, 0);
    send_dhcp(&pkt, pkt_len);

    /* ── Step 2: Wait for OFFER ── */
    if (recv_dhcp(&pkt, xid, 5000) < 0) {
        hal_console_puts("[dhcp] No OFFER received (timeout)\n");
        return HAL_TIMEOUT;
    }

    uint8_t msg_type = parse_dhcp_options(&pkt, &lease);
    if (msg_type != DHCP_OFFER) {
        hal_console_puts("[dhcp] Expected OFFER, got other\n");
        return HAL_ERROR;
    }

    hal_console_printf("[dhcp] OFFER: IP %u.%u.%u.%u\n",
                       (lease.client_ip >> 24) & 0xFF,
                       (lease.client_ip >> 16) & 0xFF,
                       (lease.client_ip >> 8) & 0xFF,
                       lease.client_ip & 0xFF);

    /* ── Step 3: DHCP Request ── */
    hal_console_puts("[dhcp] Sending REQUEST...\n");
    pkt_len = build_dhcp_packet(&pkt, DHCP_REQUEST, xid,
                                 lease.client_ip, lease.server_ip);
    send_dhcp(&pkt, pkt_len);

    /* ── Step 4: Wait for ACK ── */
    if (recv_dhcp(&pkt, xid, 5000) < 0) {
        hal_console_puts("[dhcp] No ACK received (timeout)\n");
        return HAL_TIMEOUT;
    }

    msg_type = parse_dhcp_options(&pkt, &lease);
    if (msg_type != DHCP_ACK) {
        hal_console_puts("[dhcp] Expected ACK, got NAK or other\n");
        return HAL_ERROR;
    }

    /* Store lease */
    g_lease = lease;

    *ip = lease.client_ip;
    *gateway = lease.gateway;
    *dns = lease.dns_server;

    hal_console_printf("[dhcp] ACK: IP %u.%u.%u.%u GW %u.%u.%u.%u DNS %u.%u.%u.%u\n",
                       (*ip >> 24) & 0xFF, (*ip >> 16) & 0xFF,
                       (*ip >> 8) & 0xFF, *ip & 0xFF,
                       (*gateway >> 24) & 0xFF, (*gateway >> 16) & 0xFF,
                       (*gateway >> 8) & 0xFF, *gateway & 0xFF,
                       (*dns >> 24) & 0xFF, (*dns >> 16) & 0xFF,
                       (*dns >> 8) & 0xFF, *dns & 0xFF);

    return HAL_OK;
}

const dhcp_lease_t *dhcp_get_lease(void)
{
    return &g_lease;
}
