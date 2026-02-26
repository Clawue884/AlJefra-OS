// =============================================================================
// AlJefra OS — TCP Client State Machine
// =============================================================================

#include "netstack.h"

// Simple pseudo-random for initial sequence numbers
static u32 tcp_isn_counter = 0;

static u32 tcp_generate_isn(void) {
	// Use TSC for entropy
	u64 tsc = b_system(TSC, 0, 0);
	tcp_isn_counter += (u32)(tsc >> 8) + 12345;
	return tcp_isn_counter;
}

// Find a TCP connection by local/remote address tuple
static tcp_conn_t *tcp_find_conn(net_state_t *ns, u32 remote_ip, u16 remote_port, u16 local_port) {
	for (int i = 0; i < MAX_TCP_CONNS; i++) {
		tcp_conn_t *c = &ns->tcp_conns[i];
		if (c->state != TCP_CLOSED &&
		    c->remote_ip == remote_ip &&
		    c->remote_port == remote_port &&
		    c->local_port == local_port)
			return c;
	}
	return 0;
}

// Allocate a free TCP connection slot
static tcp_conn_t *tcp_alloc_conn(net_state_t *ns) {
	for (int i = 0; i < MAX_TCP_CONNS; i++) {
		if (ns->tcp_conns[i].state == TCP_CLOSED)
			return &ns->tcp_conns[i];
	}
	return 0;
}

// Build and send a TCP segment
static void tcp_send_segment(net_state_t *ns, tcp_conn_t *conn, u8 flags,
                             const u8 *data, u32 data_len) {
	u8 buf[ETH_MTU];
	tcp_header_t *tcp = (tcp_header_t *)buf;

	// TCP header (20 bytes base, optionally + MSS option)
	u32 tcp_hdr_len = 20;
	u8 has_options = (flags & TCP_SYN) ? 1 : 0;
	if (has_options)
		tcp_hdr_len = 24;	// +4 bytes for MSS option

	tcp->src_port = htons(conn->local_port);
	tcp->dst_port = htons(conn->remote_port);
	tcp->seq = htonl(conn->seq_num);
	tcp->ack = htonl(conn->ack_num);
	tcp->data_off = (u8)((tcp_hdr_len / 4) << 4);
	tcp->flags = flags;
	tcp->window = htons(TCP_WINDOW_SIZE);
	tcp->checksum = 0;
	tcp->urgent = 0;

	// Add MSS option on SYN
	if (has_options) {
		u8 *opt = buf + 20;
		opt[0] = 2;		// MSS option kind
		opt[1] = 4;		// MSS option length
		opt[2] = (TCP_MSS >> 8) & 0xFF;
		opt[3] = TCP_MSS & 0xFF;
	}

	// Copy payload
	if (data && data_len > 0)
		net_memcpy(buf + tcp_hdr_len, data, data_len);

	u32 tcp_total = tcp_hdr_len + data_len;

	// Calculate TCP checksum
	tcp->checksum = tcp_checksum(htonl(ns->local_ip), htonl(conn->remote_ip), buf, tcp_total);

	// Send via IP
	ipv4_send(ns, conn->remote_ip, IP_PROTO_TCP, buf, tcp_total);

	// Update retransmit state
	conn->last_send_time = net_get_time_ms();
}

// Parse MSS from TCP options
static u16 tcp_parse_mss(u8 *options, u32 opt_len) {
	u32 i = 0;
	while (i < opt_len) {
		u8 kind = options[i];
		if (kind == 0) break;		// End of options
		if (kind == 1) { i++; continue; }	// NOP
		if (i + 1 >= opt_len) break;
		u8 olen = options[i + 1];
		if (olen < 2 || i + olen > opt_len) break;
		if (kind == 2 && olen == 4) {
			return ((u16)options[i + 2] << 8) | options[i + 3];
		}
		i += olen;
	}
	return TCP_MSS;	// Default
}

// Handle incoming TCP segment
void tcp_handle(net_state_t *ns, u8 *pkt, u32 ip_hdr_len, u32 total_len) {
	ipv4_header_t *ip = (ipv4_header_t *)pkt;
	tcp_header_t *tcp = (tcp_header_t *)(pkt + ip_hdr_len);
	u32 tcp_len = total_len - ip_hdr_len;

	if (tcp_len < sizeof(tcp_header_t))
		return;

	u32 tcp_hdr_len = ((tcp->data_off >> 4) & 0x0F) * 4;
	if (tcp_hdr_len < 20 || tcp_hdr_len > tcp_len)
		return;

	u32 remote_ip = ntohl(ip->src_ip);
	u16 remote_port = ntohs(tcp->src_port);
	u16 local_port = ntohs(tcp->dst_port);
	u32 seq = ntohl(tcp->seq);
	u32 ack = ntohl(tcp->ack);
	u8 flags = tcp->flags;

	u32 data_len = tcp_len - tcp_hdr_len;
	u8 *data = (u8 *)tcp + tcp_hdr_len;

	tcp_conn_t *conn = tcp_find_conn(ns, remote_ip, remote_port, local_port);
	if (!conn) {
		// No connection — send RST if it's not already a RST
		if (!(flags & TCP_RST)) {
			// Create temporary conn for RST
			tcp_conn_t tmp;
			net_memset(&tmp, 0, sizeof(tmp));
			tmp.local_ip = ns->local_ip;
			tmp.local_port = local_port;
			tmp.remote_ip = remote_ip;
			tmp.remote_port = remote_port;
			if (flags & TCP_ACK) {
				tmp.seq_num = ack;
				tcp_send_segment(ns, &tmp, TCP_RST, 0, 0);
			} else {
				tmp.seq_num = 0;
				tmp.ack_num = seq + data_len;
				if (flags & TCP_SYN) tmp.ack_num++;
				tcp_send_segment(ns, &tmp, TCP_RST | TCP_ACK, 0, 0);
			}
		}
		return;
	}

	// RST received — close connection
	if (flags & TCP_RST) {
		conn->state = TCP_CLOSED;
		return;
	}

	switch (conn->state) {
	case TCP_SYN_SENT:
		// Expecting SYN+ACK
		if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
			if (ack != conn->seq_num + 1)
				break;	// Wrong ACK
			conn->seq_num = ack;
			conn->ack_num = seq + 1;
			conn->remote_window = ntohs(tcp->window);

			// Parse MSS option
			if (tcp_hdr_len > 20) {
				u8 *opts = (u8 *)tcp + 20;
				conn->remote_mss = tcp_parse_mss(opts, tcp_hdr_len - 20);
			}

			// Send ACK
			conn->state = TCP_ESTABLISHED;
			tcp_send_segment(ns, conn, TCP_ACK, 0, 0);
			conn->retries = 0;
		}
		break;

	case TCP_ESTABLISHED:
		// Validate ACK
		if (flags & TCP_ACK) {
			// Accept ACK (simplified — just update)
			// In a full impl, we'd track unacked data
		}

		// Process incoming data
		if (data_len > 0) {
			// Check sequence number
			if (seq == conn->ack_num) {
				// In-order data — copy to receive buffer
				u32 space = TCP_RX_BUF_SIZE - ((conn->rx_tail - conn->rx_head) & (TCP_RX_BUF_SIZE - 1));
				u32 copy = data_len < space ? data_len : space;
				for (u32 i = 0; i < copy; i++) {
					conn->rx_buf[conn->rx_tail & (TCP_RX_BUF_SIZE - 1)] = data[i];
					conn->rx_tail++;
				}
				conn->ack_num += copy;
			}
			// Send ACK for received data
			tcp_send_segment(ns, conn, TCP_ACK, 0, 0);
		}

		// Handle FIN
		if (flags & TCP_FIN) {
			conn->ack_num = seq + data_len + 1;
			tcp_send_segment(ns, conn, TCP_ACK, 0, 0);
			conn->state = TCP_CLOSE_WAIT;
			// Immediately send our FIN
			tcp_send_segment(ns, conn, TCP_FIN | TCP_ACK, 0, 0);
			conn->seq_num++;
			conn->state = TCP_LAST_ACK;
		}
		break;

	case TCP_FIN_WAIT_1:
		if (flags & TCP_ACK) {
			if (flags & TCP_FIN) {
				// Simultaneous close
				conn->ack_num = seq + 1;
				tcp_send_segment(ns, conn, TCP_ACK, 0, 0);
				conn->state = TCP_TIME_WAIT;
			} else {
				conn->state = TCP_FIN_WAIT_2;
			}
		}
		break;

	case TCP_FIN_WAIT_2:
		if (flags & TCP_FIN) {
			conn->ack_num = seq + 1;
			tcp_send_segment(ns, conn, TCP_ACK, 0, 0);
			conn->state = TCP_TIME_WAIT;
		}
		break;

	case TCP_LAST_ACK:
		if (flags & TCP_ACK) {
			conn->state = TCP_CLOSED;
		}
		break;

	case TCP_TIME_WAIT:
		// Re-ACK any FIN
		if (flags & TCP_FIN) {
			tcp_send_segment(ns, conn, TCP_ACK, 0, 0);
		}
		break;
	}
}

// Open a TCP connection (client)
tcp_conn_t *tcp_connect(u32 ip, u16 port) {
	net_state_t *ns = net_get_state();

	// ARP resolve first — may need multiple polls
	u8 mac[ETH_ALEN];
	int arp_tries = 0;
	while (arp_resolve(ns, ip, mac) != 0) {
		// Wait for ARP reply
		for (int i = 0; i < 100; i++)
			net_poll();
		arp_tries++;
		if (arp_tries > 30)
			return 0;	// ARP timeout
	}

	tcp_conn_t *conn = tcp_alloc_conn(ns);
	if (!conn)
		return 0;

	net_memset(conn, 0, sizeof(tcp_conn_t));
	conn->state = TCP_SYN_SENT;
	conn->local_ip = ns->local_ip;
	conn->local_port = ns->next_port++;
	if (ns->next_port < 49152) ns->next_port = 49152;
	conn->remote_ip = ip;
	conn->remote_port = port;
	conn->seq_num = tcp_generate_isn();
	conn->ack_num = 0;
	conn->remote_mss = TCP_MSS;
	conn->retries = 0;

	// Send SYN
	tcp_send_segment(ns, conn, TCP_SYN, 0, 0);

	// Wait for SYN+ACK (up to 5 seconds, with retransmits)
	u64 start = net_get_time_ms();
	while (conn->state == TCP_SYN_SENT) {
		net_poll();
		u64 now = net_get_time_ms();
		if (now - start > 5000) {
			conn->state = TCP_CLOSED;
			return 0;
		}
		// Retransmit SYN every 1 second
		if (now - conn->last_send_time > 1000) {
			tcp_send_segment(ns, conn, TCP_SYN, 0, 0);
			conn->retries++;
			if (conn->retries > 5) {
				conn->state = TCP_CLOSED;
				return 0;
			}
		}
	}

	if (conn->state != TCP_ESTABLISHED) {
		conn->state = TCP_CLOSED;
		return 0;
	}

	return conn;
}

// Send data on an established TCP connection
int tcp_send(tcp_conn_t *conn, const void *data, u32 len) {
	if (!conn || conn->state != TCP_ESTABLISHED)
		return -1;

	net_state_t *ns = net_get_state();
	const u8 *ptr = (const u8 *)data;
	u32 sent = 0;
	u32 mss = conn->remote_mss;
	if (mss == 0) mss = TCP_MSS;

	while (sent < len) {
		u32 chunk = len - sent;
		if (chunk > mss) chunk = mss;

		tcp_send_segment(ns, conn, TCP_ACK | TCP_PSH, ptr + sent, chunk);
		conn->seq_num += chunk;
		sent += chunk;

		// Simple flow control: poll between segments
		net_poll();
	}

	return (int)sent;
}

// Receive data from a TCP connection (non-blocking)
int tcp_recv(tcp_conn_t *conn, void *buf, u32 max_len) {
	if (!conn)
		return -1;

	// Allow reads in CLOSE_WAIT/LAST_ACK if buffer has data
	if (conn->state != TCP_ESTABLISHED &&
	    conn->state != TCP_CLOSE_WAIT &&
	    conn->state != TCP_LAST_ACK) {
		if (conn->rx_head == conn->rx_tail)
			return -1;
	}

	u32 avail = conn->rx_tail - conn->rx_head;
	if (avail == 0)
		return 0;

	u32 copy = avail < max_len ? avail : max_len;
	u8 *dst = (u8 *)buf;
	for (u32 i = 0; i < copy; i++) {
		dst[i] = conn->rx_buf[conn->rx_head & (TCP_RX_BUF_SIZE - 1)];
		conn->rx_head++;
	}
	return (int)copy;
}

// Close a TCP connection
void tcp_close(tcp_conn_t *conn) {
	if (!conn)
		return;

	net_state_t *ns = net_get_state();

	if (conn->state == TCP_ESTABLISHED) {
		// Send FIN
		tcp_send_segment(ns, conn, TCP_FIN | TCP_ACK, 0, 0);
		conn->seq_num++;
		conn->state = TCP_FIN_WAIT_1;

		// Wait for close to complete (up to 3 seconds)
		u64 start = net_get_time_ms();
		while (conn->state != TCP_CLOSED && conn->state != TCP_TIME_WAIT) {
			net_poll();
			if (net_get_time_ms() - start > 3000)
				break;
		}
	}

	conn->state = TCP_CLOSED;
}

// Periodic TCP timer tick — handle retransmissions and TIME_WAIT cleanup
void tcp_tick(net_state_t *ns) {
	u64 now = net_get_time_ms();

	for (int i = 0; i < MAX_TCP_CONNS; i++) {
		tcp_conn_t *conn = &ns->tcp_conns[i];

		// TIME_WAIT timeout (2 seconds)
		if (conn->state == TCP_TIME_WAIT) {
			if (now - conn->last_send_time > 2000)
				conn->state = TCP_CLOSED;
		}
	}
}
