/* SPDX-License-Identifier: MIT */
/* AlJefra OS — Minimal TCP Client
 *
 * Single-connection TCP client for HTTP marketplace communication.
 * Uses the HAL network driver (raw Ethernet frames).
 */

#ifndef ALJEFRA_TCP_H
#define ALJEFRA_TCP_H

#include <stdint.h>
#include "../hal/hal.h"

/* TCP connection states */
#define TCP_CLOSED       0
#define TCP_SYN_SENT     1
#define TCP_ESTABLISHED  2
#define TCP_FIN_WAIT_1   3
#define TCP_FIN_WAIT_2   4
#define TCP_TIME_WAIT    5
#define TCP_CLOSE_WAIT   6

/* TCP receive buffer size */
#define TCP_RX_BUF_SIZE  8192
#define TCP_TX_MSS       1460

typedef struct {
    uint8_t  state;
    uint32_t local_ip;
    uint16_t local_port;
    uint32_t remote_ip;
    uint16_t remote_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint16_t remote_mss;
    uint8_t  local_mac[6];
    uint8_t  remote_mac[6];  /* Gateway MAC (resolved via ARP) */
    uint8_t  rx_buf[TCP_RX_BUF_SIZE];
    uint32_t rx_len;         /* Bytes available in rx_buf */
} tcp_conn_t;

/* Initialize TCP subsystem with network config from DHCP */
void tcp_init(uint32_t local_ip, uint32_t gateway, uint32_t netmask);

/* Connect to remote host. Returns HAL_OK on success. */
hal_status_t tcp_connect(tcp_conn_t *conn, uint32_t remote_ip, uint16_t remote_port);

/* Send data over established connection. Returns bytes sent or -1. */
int32_t tcp_send(tcp_conn_t *conn, const void *data, uint32_t len);

/* Receive data. Returns bytes received, 0 if no data yet, -1 on error. */
int32_t tcp_recv(tcp_conn_t *conn, void *buf, uint32_t max_len, uint32_t timeout_ms);

/* Close connection gracefully. */
void tcp_close(tcp_conn_t *conn);

#endif /* ALJEFRA_TCP_H */
