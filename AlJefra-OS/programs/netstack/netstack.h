// =============================================================================
// AlJefra OS — Minimal TCP/IP Stack
// =============================================================================

#ifndef NETSTACK_H
#define NETSTACK_H

#include "libAlJefra.h"

// Ethernet
#define ETH_ALEN	6
#define ETH_HLEN	14
#define ETH_MTU		1500
#define ETH_FRAME_MAX	1522

#define ETH_TYPE_ARP	0x0806
#define ETH_TYPE_IP	0x0800

// IP Protocol numbers
#define IP_PROTO_ICMP	1
#define IP_PROTO_TCP	6
#define IP_PROTO_UDP	17

// TCP states
#define TCP_CLOSED	0
#define TCP_SYN_SENT	1
#define TCP_ESTABLISHED	2
#define TCP_FIN_WAIT_1	3
#define TCP_FIN_WAIT_2	4
#define TCP_TIME_WAIT	5
#define TCP_CLOSE_WAIT	6
#define TCP_LAST_ACK	7

// TCP flags
#define TCP_FIN		0x01
#define TCP_SYN		0x02
#define TCP_RST		0x04
#define TCP_PSH		0x08
#define TCP_ACK		0x10
#define TCP_URG		0x20

// Sizes
#define MAX_TCP_CONNS	8
#define TCP_WINDOW_SIZE	8192
#define TCP_MSS		1460
#define TCP_RX_BUF_SIZE	8192
#define TCP_TX_BUF_SIZE	8192
#define ARP_CACHE_SIZE	16
#define DNS_CACHE_SIZE	4

// Network byte order
#define htons(x) __builtin_bswap16(x)
#define ntohs(x) __builtin_bswap16(x)
#define htonl(x) __builtin_bswap32(x)
#define ntohl(x) __builtin_bswap32(x)

// Build an IPv4 address from 4 octets
#define IP4(a,b,c,d) (((u32)(a)<<24)|((u32)(b)<<16)|((u32)(c)<<8)|(u32)(d))

// ---- Structures ----

typedef struct {
	u8 dst[ETH_ALEN];
	u8 src[ETH_ALEN];
	u16 type;
} __attribute__((packed)) eth_header_t;

typedef struct {
	u8 ver_ihl;
	u8 tos;
	u16 total_len;
	u16 id;
	u16 flags_frag;
	u8 ttl;
	u8 protocol;
	u16 checksum;
	u32 src_ip;
	u32 dst_ip;
} __attribute__((packed)) ipv4_header_t;

typedef struct {
	u16 src_port;
	u16 dst_port;
	u32 seq;
	u32 ack;
	u8 data_off;		// upper 4 bits = offset in 32-bit words
	u8 flags;
	u16 window;
	u16 checksum;
	u16 urgent;
} __attribute__((packed)) tcp_header_t;

typedef struct {
	u16 src_port;
	u16 dst_port;
	u16 length;
	u16 checksum;
} __attribute__((packed)) udp_header_t;

typedef struct {
	u8 type;
	u8 code;
	u16 checksum;
	u16 id;
	u16 seq;
} __attribute__((packed)) icmp_header_t;

typedef struct {
	u16 hw_type;
	u16 proto_type;
	u8 hw_len;
	u8 proto_len;
	u16 opcode;
	u8 sender_mac[ETH_ALEN];
	u32 sender_ip;
	u8 target_mac[ETH_ALEN];
	u32 target_ip;
} __attribute__((packed)) arp_packet_t;

// TCP connection
typedef struct {
	u8 state;
	u32 local_ip;
	u16 local_port;
	u32 remote_ip;
	u16 remote_port;
	u32 seq_num;
	u32 ack_num;
	u16 remote_window;
	u16 remote_mss;
	u64 last_send_time;	// For retransmission
	u8 retries;
	// Receive buffer (ring)
	u8 rx_buf[TCP_RX_BUF_SIZE];
	u32 rx_head;
	u32 rx_tail;
	// Retransmit state
	u8 tx_buf[TCP_TX_BUF_SIZE];
	u32 tx_len;
} tcp_conn_t;

// ARP cache entry
typedef struct {
	u32 ip;
	u8 mac[ETH_ALEN];
	u8 valid;
	u8 _pad;
} arp_entry_t;

// DNS cache entry
typedef struct {
	char name[64];
	u32 ip;
	u8 valid;
} dns_entry_t;

// Global network state
typedef struct {
	u32 local_ip;
	u32 gateway;
	u32 netmask;
	u32 dns_server;
	u8 local_mac[ETH_ALEN];
	u16 next_port;		// Next ephemeral port
	u16 ip_id;		// Next IP identification
	u64 iid;		// Interface ID for b_net_*
	arp_entry_t arp_cache[ARP_CACHE_SIZE];
	tcp_conn_t tcp_conns[MAX_TCP_CONNS];
	dns_entry_t dns_cache[DNS_CACHE_SIZE];
	// Scratch buffers
	u8 tx_frame[ETH_FRAME_MAX];
	u8 rx_copy[ETH_FRAME_MAX];
} net_state_t;

// ---- Public API ----

// netstack.c
int net_init(u32 ip, u32 gateway, u32 netmask, u32 dns);
void net_poll(void);
u64 net_get_time_ms(void);

// arp.c
void arp_handle(net_state_t *ns, u8 *pkt, u32 len);
int arp_resolve(net_state_t *ns, u32 ip, u8 *mac_out);
void arp_request(net_state_t *ns, u32 target_ip);

// ipv4.c
void ipv4_handle(net_state_t *ns, u8 *pkt, u32 len);
void ipv4_send(net_state_t *ns, u32 dst_ip, u8 proto, u8 *payload, u32 payload_len);

// icmp.c
void icmp_handle(net_state_t *ns, u8 *pkt, u32 ip_hdr_len, u32 total_len);
int icmp_ping(u32 ip, u32 timeout_ms);

// udp.c
void udp_handle(net_state_t *ns, u8 *pkt, u32 ip_hdr_len, u32 total_len);
int udp_send(net_state_t *ns, u32 dst_ip, u16 src_port, u16 dst_port, u8 *data, u32 len);

// tcp.c
void tcp_handle(net_state_t *ns, u8 *pkt, u32 ip_hdr_len, u32 total_len);
tcp_conn_t *tcp_connect(u32 ip, u16 port);
int tcp_send(tcp_conn_t *conn, const void *data, u32 len);
int tcp_recv(tcp_conn_t *conn, void *buf, u32 max_len);
void tcp_close(tcp_conn_t *conn);
void tcp_tick(net_state_t *ns);

// dns.c
u32 dns_resolve(const char *hostname);

// checksum.c
u16 ip_checksum(void *data, u32 len);
u16 tcp_checksum(u32 src_ip, u32 dst_ip, void *tcp_data, u32 tcp_len);
u16 udp_checksum(u32 src_ip, u32 dst_ip, void *udp_data, u32 udp_len);

// util.c
void *net_memcpy(void *dst, const void *src, u64 n);
void *net_memset(void *dst, int c, u64 n);
void *net_memmove(void *dst, const void *src, u64 n);
int net_memcmp(const void *a, const void *b, u64 n);
u32 net_strlen(const char *s);
int net_strcmp(const char *a, const char *b);
int net_strncmp(const char *a, const char *b, u64 n);
char *net_strcpy(char *dst, const char *src);
int mini_sprintf(char *buf, const char *fmt, ...);

// Global state accessor
net_state_t *net_get_state(void);

#endif
