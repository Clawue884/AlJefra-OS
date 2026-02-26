/* SPDX-License-Identifier: MIT */
/* AlJefra OS — Minimal TCP Client Implementation
 *
 * Single-connection TCP client for kernel marketplace communication.
 * Operates on raw Ethernet frames via the HAL network driver.
 *
 * Implements:
 *   - ARP resolution (to find gateway MAC)
 *   - TCP 3-way handshake (SYN → SYN-ACK → ACK)
 *   - TCP data transfer (PSH|ACK, with ACKs)
 *   - TCP close (FIN → FIN-ACK)
 *
 * Reference: RFC 793 (TCP), RFC 826 (ARP)
 */

#include "tcp.h"
#include "checksum.h"
#include "../hal/hal.h"
#include "../kernel/driver_loader.h"
#include "../lib/string.h"
#include "../lib/endian.h"

/* ── TCP flags ── */
#define TCP_FIN  0x01
#define TCP_SYN  0x02
#define TCP_RST  0x04
#define TCP_PSH  0x08
#define TCP_ACK  0x10

/* ── Protocol numbers ── */
#define ETHERTYPE_ARP  0x0806
#define ETHERTYPE_IPV4 0x0800
#define IP_PROTO_TCP   6

/* ── ARP ── */
#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY   2

typedef struct __attribute__((packed)) {
    /* Ethernet header */
    uint8_t  eth_dst[6];
    uint8_t  eth_src[6];
    uint16_t eth_type;
    /* ARP */
    uint16_t hw_type;     /* 1 = Ethernet */
    uint16_t proto_type;  /* 0x0800 = IPv4 */
    uint8_t  hw_len;      /* 6 */
    uint8_t  proto_len;   /* 4 */
    uint16_t opcode;
    uint8_t  sender_mac[6];
    uint32_t sender_ip;
    uint8_t  target_mac[6];
    uint32_t target_ip;
} arp_frame_t;

/* ── TCP/IP frame header ── */
typedef struct __attribute__((packed)) {
    /* Ethernet */
    uint8_t  eth_dst[6];
    uint8_t  eth_src[6];
    uint16_t eth_type;
    /* IPv4 (20 bytes, no options) */
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
    /* TCP (20 bytes, no options) */
    uint16_t tcp_src;
    uint16_t tcp_dst;
    uint32_t tcp_seq;
    uint32_t tcp_ack;
    uint8_t  tcp_data_off;   /* upper 4 bits = header length in 32-bit words */
    uint8_t  tcp_flags;
    uint16_t tcp_window;
    uint16_t tcp_checksum;
    uint16_t tcp_urgent;
} tcp_frame_t;

/* Size of headers */
#define ETH_HDR_LEN   14
#define IP_HDR_LEN    20
#define TCP_HDR_LEN   20
#define TCP_FRAME_HDR (ETH_HDR_LEN + IP_HDR_LEN + TCP_HDR_LEN)

/* ── Module state ── */
static uint32_t g_local_ip;
static uint32_t g_gateway;
static uint32_t g_netmask;
static uint8_t  g_local_mac[6];
static uint16_t g_ip_id = 1;

/* TCP pseudo-header checksum */
static uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip,
                              const void *tcp_hdr, uint32_t tcp_len)
{
    uint32_t sum = 0;

    /* Pseudo-header: src_ip, dst_ip, zero, proto=6, tcp_length */
    sum += (src_ip >> 16) & 0xFFFF;
    sum += src_ip & 0xFFFF;
    sum += (dst_ip >> 16) & 0xFFFF;
    sum += dst_ip & 0xFFFF;
    sum += htons(IP_PROTO_TCP);
    sum += htons((uint16_t)tcp_len);

    /* TCP header + data */
    const uint16_t *p = (const uint16_t *)tcp_hdr;
    uint32_t remaining = tcp_len;
    while (remaining > 1) {
        sum += *p++;
        remaining -= 2;
    }
    if (remaining == 1)
        sum += *(const uint8_t *)p;

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

/* ── Send a raw TCP frame ── */
static void send_tcp_frame(tcp_conn_t *conn, uint8_t flags,
                            const void *data, uint32_t data_len)
{
    const driver_ops_t *net = driver_get_network();
    if (!net || !net->net_tx) return;

    uint8_t frame[1514]; /* Max Ethernet frame */
    memset(frame, 0, sizeof(frame));

    tcp_frame_t *f = (tcp_frame_t *)frame;

    /* Ethernet */
    memcpy(f->eth_dst, conn->remote_mac, 6);
    memcpy(f->eth_src, conn->local_mac, 6);
    f->eth_type = htons(ETHERTYPE_IPV4);

    /* IPv4 */
    uint16_t tcp_total = TCP_HDR_LEN + data_len;
    uint16_t ip_total = IP_HDR_LEN + tcp_total;
    f->ip_ver_ihl = 0x45;
    f->ip_len = htons(ip_total);
    f->ip_id = htons(g_ip_id++);
    f->ip_frag = htons(0x4000); /* Don't Fragment */
    f->ip_ttl = 64;
    f->ip_proto = IP_PROTO_TCP;
    f->ip_src = htonl(conn->local_ip);
    f->ip_dst = htonl(conn->remote_ip);
    f->ip_checksum = 0;
    f->ip_checksum = ip_checksum(&f->ip_ver_ihl, IP_HDR_LEN);

    /* TCP */
    f->tcp_src = htons(conn->local_port);
    f->tcp_dst = htons(conn->remote_port);
    f->tcp_seq = htonl(conn->seq_num);
    f->tcp_ack = htonl(conn->ack_num);
    f->tcp_data_off = (TCP_HDR_LEN / 4) << 4; /* 5 words = 20 bytes */
    f->tcp_flags = flags;
    f->tcp_window = htons(TCP_RX_BUF_SIZE);
    f->tcp_urgent = 0;

    /* Copy payload after TCP header */
    if (data && data_len > 0)
        memcpy(frame + TCP_FRAME_HDR, data, data_len);

    /* TCP checksum (over pseudo-header + TCP header + data) */
    f->tcp_checksum = 0;
    f->tcp_checksum = tcp_checksum(htonl(conn->local_ip), htonl(conn->remote_ip),
                                    &f->tcp_src, tcp_total);

    uint32_t total = ETH_HDR_LEN + ip_total;
    if (total < 60) total = 60; /* Minimum Ethernet frame */

    net->net_tx(frame, total);
}

/* ── Receive and parse a TCP frame (returns payload length, -1 on timeout) ── */
static int32_t recv_tcp_frame(tcp_conn_t *conn, uint8_t *out_flags,
                               uint32_t *out_seq, uint32_t *out_ack,
                               void *payload, uint32_t max_payload,
                               uint32_t timeout_ms)
{
    const driver_ops_t *net = driver_get_network();
    if (!net || !net->net_rx) return -1;

    uint64_t deadline = hal_timer_ms() + timeout_ms;
    uint8_t frame[1514];

    while (hal_timer_ms() < deadline) {
        int64_t len = net->net_rx(frame, sizeof(frame));
        if (len <= 0) {
            hal_timer_delay_us(100); /* 0.1ms poll */
            continue;
        }

        /* Must be at least Ethernet + IP + TCP headers */
        if (len < (int64_t)TCP_FRAME_HDR)
            continue;

        tcp_frame_t *f = (tcp_frame_t *)frame;

        /* Check Ethernet type = IPv4 */
        if (ntohs(f->eth_type) != ETHERTYPE_IPV4)
            continue;

        /* Check IP protocol = TCP */
        if (f->ip_proto != IP_PROTO_TCP)
            continue;

        /* Check IP addresses match our connection */
        if (ntohl(f->ip_src) != conn->remote_ip)
            continue;
        if (ntohl(f->ip_dst) != conn->local_ip)
            continue;

        /* Check TCP ports */
        if (ntohs(f->tcp_src) != conn->remote_port)
            continue;
        if (ntohs(f->tcp_dst) != conn->local_port)
            continue;

        /* Extract fields */
        *out_flags = f->tcp_flags;
        *out_seq = ntohl(f->tcp_seq);
        *out_ack = ntohl(f->tcp_ack);

        /* Calculate payload offset and length */
        uint32_t ip_hdr_len = (f->ip_ver_ihl & 0x0F) * 4;
        uint32_t tcp_hdr_len = (f->tcp_data_off >> 4) * 4;
        uint32_t hdr_total = ETH_HDR_LEN + ip_hdr_len + tcp_hdr_len;
        uint32_t payload_len = 0;
        if ((uint32_t)len > hdr_total)
            payload_len = (uint32_t)len - hdr_total;

        /* Clamp to IP total length */
        uint16_t ip_total = ntohs(f->ip_len);
        uint32_t ip_payload = 0;
        if (ip_total > ip_hdr_len + tcp_hdr_len)
            ip_payload = ip_total - ip_hdr_len - tcp_hdr_len;
        if (payload_len > ip_payload)
            payload_len = ip_payload;

        if (payload && payload_len > 0) {
            uint32_t copy_len = payload_len < max_payload ? payload_len : max_payload;
            memcpy(payload, frame + hdr_total, copy_len);
            payload_len = copy_len;
        }

        return (int32_t)payload_len;
    }

    return -1; /* Timeout */
}

/* ── ARP resolve ── */
static hal_status_t arp_resolve(uint32_t target_ip, uint8_t *target_mac)
{
    const driver_ops_t *net = driver_get_network();
    if (!net || !net->net_tx || !net->net_rx) return HAL_NO_DEVICE;

    hal_console_printf("[tcp] ARP: resolving %u.%u.%u.%u\n",
                       (target_ip >> 24) & 0xFF, (target_ip >> 16) & 0xFF,
                       (target_ip >> 8) & 0xFF, target_ip & 0xFF);

    /* Build ARP request */
    arp_frame_t arp;
    memset(&arp, 0, sizeof(arp));

    /* Ethernet: broadcast */
    for (int i = 0; i < 6; i++) arp.eth_dst[i] = 0xFF;
    memcpy(arp.eth_src, g_local_mac, 6);
    arp.eth_type = htons(ETHERTYPE_ARP);

    /* ARP */
    arp.hw_type = htons(1);          /* Ethernet */
    arp.proto_type = htons(0x0800);  /* IPv4 */
    arp.hw_len = 6;
    arp.proto_len = 4;
    arp.opcode = htons(ARP_OP_REQUEST);
    memcpy(arp.sender_mac, g_local_mac, 6);
    arp.sender_ip = htonl(g_local_ip);
    memset(arp.target_mac, 0, 6);
    arp.target_ip = htonl(target_ip);

    /* Send ARP request (try up to 3 times) */
    for (int attempt = 0; attempt < 3; attempt++) {
        net->net_tx(&arp, sizeof(arp));

        /* Wait for ARP reply */
        uint64_t deadline = hal_timer_ms() + 2000; /* 2 second timeout */
        uint8_t frame[1514];

        while (hal_timer_ms() < deadline) {
            int64_t len = net->net_rx(frame, sizeof(frame));
            if (len <= 0) {
                hal_timer_delay_us(100);
                continue;
            }

            if (len < (int64_t)sizeof(arp_frame_t))
                continue;

            arp_frame_t *reply = (arp_frame_t *)frame;

            /* Check: ARP reply for our IP */
            if (ntohs(reply->eth_type) != ETHERTYPE_ARP)
                continue;
            if (ntohs(reply->opcode) != ARP_OP_REPLY)
                continue;
            if (ntohl(reply->sender_ip) != target_ip)
                continue;

            /* Got it! */
            memcpy(target_mac, reply->sender_mac, 6);
            hal_console_printf("[tcp] ARP: resolved to %02x:%02x:%02x:%02x:%02x:%02x\n",
                               target_mac[0], target_mac[1], target_mac[2],
                               target_mac[3], target_mac[4], target_mac[5]);
            return HAL_OK;
        }

        hal_console_printf("[tcp] ARP: attempt %d timeout, retrying...\n", attempt + 1);
    }

    hal_console_puts("[tcp] ARP: failed to resolve\n");
    return HAL_TIMEOUT;
}

/* ── Public API ── */

void tcp_init(uint32_t local_ip, uint32_t gateway, uint32_t netmask)
{
    g_local_ip = local_ip;
    g_gateway = gateway;
    g_netmask = netmask;
    g_ip_id = (uint16_t)(hal_timer_ns() & 0xFFFF);

    const driver_ops_t *net = driver_get_network();
    if (net && net->net_get_mac)
        net->net_get_mac(g_local_mac);

    hal_console_printf("[tcp] Initialized: IP %u.%u.%u.%u GW %u.%u.%u.%u\n",
                       (local_ip >> 24) & 0xFF, (local_ip >> 16) & 0xFF,
                       (local_ip >> 8) & 0xFF, local_ip & 0xFF,
                       (gateway >> 24) & 0xFF, (gateway >> 16) & 0xFF,
                       (gateway >> 8) & 0xFF, gateway & 0xFF);
}

hal_status_t tcp_connect(tcp_conn_t *conn, uint32_t remote_ip, uint16_t remote_port)
{
    memset(conn, 0, sizeof(*conn));
    conn->local_ip = g_local_ip;
    conn->remote_ip = remote_ip;
    conn->remote_port = remote_port;
    conn->local_port = 49152 + (uint16_t)(hal_timer_ns() & 0x3FFF); /* Ephemeral port */
    conn->seq_num = (uint32_t)(hal_timer_ns() & 0xFFFFFFFF);
    conn->ack_num = 0;
    conn->remote_mss = TCP_TX_MSS;
    conn->rx_len = 0;
    memcpy(conn->local_mac, g_local_mac, 6);

    hal_console_printf("[tcp] Connecting to %u.%u.%u.%u:%u\n",
                       (remote_ip >> 24) & 0xFF, (remote_ip >> 16) & 0xFF,
                       (remote_ip >> 8) & 0xFF, remote_ip & 0xFF,
                       remote_port);

    /* Determine next-hop IP: if remote is on same subnet, use it directly;
     * otherwise use gateway */
    uint32_t next_hop;
    if ((remote_ip & g_netmask) == (g_local_ip & g_netmask))
        next_hop = remote_ip;
    else
        next_hop = g_gateway;

    /* ARP resolve the next-hop */
    hal_status_t rc = arp_resolve(next_hop, conn->remote_mac);
    if (rc != HAL_OK)
        return rc;

    /* TCP 3-way handshake: send SYN */
    conn->state = TCP_SYN_SENT;
    send_tcp_frame(conn, TCP_SYN, NULL, 0);
    conn->seq_num++; /* SYN consumes one sequence number */

    hal_console_puts("[tcp] SYN sent, waiting for SYN-ACK...\n");

    /* Wait for SYN-ACK */
    uint8_t flags;
    uint32_t rseq, rack;
    for (int attempt = 0; attempt < 3; attempt++) {
        int32_t plen = recv_tcp_frame(conn, &flags, &rseq, &rack, NULL, 0, 5000);
        if (plen < 0) {
            /* Timeout — retransmit SYN */
            conn->seq_num--; /* Back up to re-send same SYN */
            send_tcp_frame(conn, TCP_SYN, NULL, 0);
            conn->seq_num++;
            hal_console_puts("[tcp] SYN-ACK timeout, retransmitting SYN...\n");
            continue;
        }

        if (flags & TCP_RST) {
            hal_console_puts("[tcp] Connection refused (RST)\n");
            conn->state = TCP_CLOSED;
            return HAL_ERROR;
        }

        if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
            /* SYN-ACK received */
            conn->ack_num = rseq + 1; /* ACK the SYN */

            /* Send ACK to complete handshake */
            send_tcp_frame(conn, TCP_ACK, NULL, 0);

            conn->state = TCP_ESTABLISHED;
            hal_console_puts("[tcp] Connection established\n");
            return HAL_OK;
        }
    }

    hal_console_puts("[tcp] Connection failed (no SYN-ACK)\n");
    conn->state = TCP_CLOSED;
    return HAL_TIMEOUT;
}

int32_t tcp_send(tcp_conn_t *conn, const void *data, uint32_t len)
{
    if (conn->state != TCP_ESTABLISHED)
        return -1;

    const uint8_t *p = (const uint8_t *)data;
    uint32_t sent = 0;

    while (sent < len) {
        uint32_t chunk = len - sent;
        if (chunk > TCP_TX_MSS)
            chunk = TCP_TX_MSS;

        send_tcp_frame(conn, TCP_PSH | TCP_ACK, p + sent, chunk);
        conn->seq_num += chunk;
        sent += chunk;
    }

    return (int32_t)sent;
}

int32_t tcp_recv(tcp_conn_t *conn, void *buf, uint32_t max_len, uint32_t timeout_ms)
{
    if (conn->state != TCP_ESTABLISHED && conn->state != TCP_CLOSE_WAIT)
        return -1;

    uint8_t *out = (uint8_t *)buf;
    uint32_t total = 0;
    uint64_t deadline = hal_timer_ms() + timeout_ms;
    uint8_t got_data = 0;

    while (hal_timer_ms() < deadline && total < max_len) {
        uint8_t flags;
        uint32_t rseq, rack;
        uint8_t tmp[1460];

        /* Short poll timeout: 100ms or remaining time */
        uint32_t remain = (uint32_t)(deadline - hal_timer_ms());
        uint32_t poll_timeout = remain < 200 ? remain : 200;

        int32_t plen = recv_tcp_frame(conn, &flags, &rseq, &rack, tmp, sizeof(tmp),
                                       poll_timeout);

        if (plen < 0) {
            /* Timeout — if we already have data, return it */
            if (got_data)
                break;
            continue;
        }

        /* Handle RST */
        if (flags & TCP_RST) {
            conn->state = TCP_CLOSED;
            if (total > 0) return (int32_t)total;
            return -1;
        }

        /* Handle FIN */
        if (flags & TCP_FIN) {
            conn->ack_num = rseq + (uint32_t)plen + 1; /* ACK the FIN */
            send_tcp_frame(conn, TCP_ACK, NULL, 0);
            conn->state = TCP_CLOSE_WAIT;
            if (plen > 0) {
                uint32_t copy = (uint32_t)plen;
                if (total + copy > max_len)
                    copy = max_len - total;
                memcpy(out + total, tmp, copy);
                total += copy;
            }
            break;
        }

        /* Copy payload data */
        if (plen > 0) {
            /* Update ACK number */
            conn->ack_num = rseq + (uint32_t)plen;

            uint32_t copy = (uint32_t)plen;
            if (total + copy > max_len)
                copy = max_len - total;
            memcpy(out + total, tmp, copy);
            total += copy;
            got_data = 1;

            /* Send ACK */
            send_tcp_frame(conn, TCP_ACK, NULL, 0);
        }
    }

    return (int32_t)total;
}

void tcp_close(tcp_conn_t *conn)
{
    if (conn->state == TCP_ESTABLISHED) {
        /* Send FIN */
        send_tcp_frame(conn, TCP_FIN | TCP_ACK, NULL, 0);
        conn->seq_num++; /* FIN consumes one sequence number */
        conn->state = TCP_FIN_WAIT_1;

        /* Wait for FIN-ACK (best effort, don't block forever) */
        uint8_t flags;
        uint32_t rseq, rack;
        for (int i = 0; i < 5; i++) {
            int32_t plen = recv_tcp_frame(conn, &flags, &rseq, &rack, NULL, 0, 1000);
            if (plen < 0)
                continue;

            if (flags & TCP_RST)
                break;

            if (flags & TCP_FIN) {
                conn->ack_num = rseq + 1;
                send_tcp_frame(conn, TCP_ACK, NULL, 0);
                break;
            }

            if (flags & TCP_ACK) {
                conn->state = TCP_FIN_WAIT_2;
                /* Continue waiting for FIN */
            }
        }
    } else if (conn->state == TCP_CLOSE_WAIT) {
        /* Send FIN to complete close */
        send_tcp_frame(conn, TCP_FIN | TCP_ACK, NULL, 0);
        conn->seq_num++;

        /* Wait for ACK */
        uint8_t flags;
        uint32_t rseq, rack;
        recv_tcp_frame(conn, &flags, &rseq, &rack, NULL, 0, 2000);
    }

    conn->state = TCP_CLOSED;
    hal_console_puts("[tcp] Connection closed\n");
}
