# AlJefra OS -- Network Stack

## Overview

AlJefra OS includes a full TCP/IP network stack built directly into the kernel. Networking is not optional -- it is fundamental to the OS's operation. The AI bootstrap system requires HTTPS connectivity to download drivers from the marketplace, the AI chat engine uses HTTPS to communicate with LLM backends, and OTA updates are delivered over the network.

The network stack (TCP/IP, UDP, ARP, DHCP, DNS, HTTP) is implemented from scratch (~5,165 lines of original code). TLS 1.2 is provided by the vendored BearSSL library (101,889 lines of third-party code).

## Protocol Layers

```
+---------------------------------------------------------------+
|  Applications                                                  |
|  AI Bootstrap, AI Chat, OTA Updates                            |
+---------------------------------------------------------------+
|  HTTP/1.1          | Request/response, chunked encoding        |
+---------------------------------------------------------------+
|  TLS 1.2           | BearSSL library, certificate validation   |
+---------------------------------------------------------------+
|  TCP                | Reliable streams, flow control            |
+--------------------+------------------------------------------+
|  UDP               | Connectionless datagrams                  |
+--------------------+------------------------------------------+
|  ICMP              | Echo request/reply (ping)                 |
+---------------------------------------------------------------+
|  IPv4              | Packet routing, fragmentation             |
+---------------------------------------------------------------+
|  ARP               | Address resolution (IPv4 -> MAC)          |
+---------------------------------------------------------------+
|  Ethernet          | Frame parsing, MAC addressing             |
+---------------------------------------------------------------+
|  NIC Drivers       | e1000, VirtIO-Net, RTL8169, WiFi          |
+---------------------------------------------------------------+
```

## NIC Drivers

The kernel includes built-in drivers for common network interface cards. Additional NIC drivers can be installed from the marketplace via `.ajdrv` packages.

| Driver | Hardware | Type | Use Case |
|--------|----------|------|----------|
| e1000 | Intel 82540EM/82574L | Wired Ethernet | QEMU default, common in VMs |
| VirtIO-Net | VirtIO network device | Wired Ethernet | KVM/QEMU paravirtualized |
| RTL8169 | Realtek RTL8169/8168 | Wired Ethernet | Common consumer NICs |
| Intel WiFi | Intel Wireless-AC/AX series | WiFi | Laptop wireless |
| Broadcom WiFi | BCM43xx series | WiFi | Laptop/embedded wireless |

All NIC drivers implement the standard `driver_ops_t` network interface:

```c
int64_t (*net_tx)(const void *frame, uint64_t len);    // Send Ethernet frame
int64_t (*net_rx)(void *frame, uint64_t max_len);      // Receive Ethernet frame
void    (*net_get_mac)(uint8_t mac[6]);                 // Get MAC address
```

## Ethernet Layer

The Ethernet layer handles frame parsing and construction:

```c
typedef struct __attribute__((packed)) {
    uint8_t  dst_mac[6];
    uint8_t  src_mac[6];
    uint16_t ethertype;      // 0x0800 = IPv4, 0x0806 = ARP
    uint8_t  payload[];
} ethernet_frame_t;
```

- Incoming frames are dispatched by EtherType to the appropriate protocol handler (ARP or IPv4).
- Outgoing frames have the source MAC filled automatically from the NIC driver.

## ARP (Address Resolution Protocol)

ARP resolves IPv4 addresses to MAC addresses on the local network:

- Maintains an ARP cache of recently resolved entries
- Sends ARP requests for unknown addresses
- Handles ARP replies and gratuitous ARP announcements
- Cache entries expire after a configurable timeout (default: 300 seconds)

## IPv4

The IPv4 layer handles packet construction, routing, and reassembly:

- Source and destination address management
- Header checksum calculation and verification
- Fragmentation for packets exceeding the MTU (1500 bytes for Ethernet)
- Fragment reassembly with timeout
- Simple routing: single default gateway (from DHCP)

## ICMP

ICMP provides diagnostic capabilities:

- **Echo Reply**: Responds to incoming ping requests (type 8 -> type 0)
- **Destination Unreachable**: Generated when no route or port is available
- Used internally for network diagnostics and connectivity testing

## UDP

Connectionless transport for DNS and DHCP:

- Port-based multiplexing
- Checksum calculation (optional per RFC 768, but always computed)
- No retransmission, no ordering guarantees
- Used by: DHCP client, DNS resolver

## TCP

**Source**: `net/tcp.c`

Connection-oriented reliable transport, used for all HTTP/TLS communication:

### Connection Lifecycle

```
Client                              Server
  |                                   |
  |  -------- SYN (seq=x) -------->  |
  |                                   |
  |  <--- SYN-ACK (seq=y,ack=x+1) - |
  |                                   |
  |  -------- ACK (ack=y+1) ------> |
  |                                   |
  |          [DATA TRANSFER]          |
  |                                   |
  |  -------- FIN -----------------> |
  |  <------- FIN-ACK -------------- |
  |  -------- ACK -----------------> |
  |                                   |
```

### Features

- **SYN/ACK three-way handshake**: Standard TCP connection establishment
- **Retransmission**: Unacknowledged segments are retransmitted with exponential backoff
- **Window management**: Sliding window flow control based on receiver's advertised window
- **Sequence number tracking**: Full 32-bit sequence number space with wrap-around handling
- **Connection timeout**: Connections that fail to complete the handshake are cleaned up
- **Graceful close**: FIN/FIN-ACK exchange for clean connection teardown

## DNS

The DNS resolver translates hostnames to IPv4 addresses:

- Sends A record queries over UDP port 53
- DNS server address obtained from DHCP
- Simple response parsing (first A record used)
- No caching (each lookup sends a fresh query)
- Used to resolve `api.aljefra.com` and `api.anthropic.com`

## DHCP

**Source**: `net/dhcp.c` (382 lines)

The DHCP client implements the full DORA flow for automatic network configuration:

### DORA Flow

```
1. Discover   Client broadcasts DHCPDISCOVER on UDP port 67
              Source: 0.0.0.0:68 -> Destination: 255.255.255.255:67
              "I need an IP address"

2. Offer      Server responds with DHCPOFFER on UDP port 68
              Contains: offered IP, subnet mask, gateway, DNS, lease time
              "You can have 192.168.1.100"

3. Request    Client broadcasts DHCPREQUEST on UDP port 67
              Contains: requested IP from the offer
              "I'll take 192.168.1.100"

4. Acknowledge Server responds with DHCPACK on UDP port 68
              Confirms the lease, provides final parameters
              "192.168.1.100 is yours for 86400 seconds"
```

### Configuration Obtained

| Parameter | DHCP Option | Usage |
|-----------|------------|-------|
| IP address | Option 50 / yiaddr | Local interface address |
| Subnet mask | Option 1 | Network/host bit boundary |
| Default gateway | Option 3 | IPv4 routing |
| DNS server | Option 6 | Name resolution |
| Lease time | Option 51 | Renewal scheduling |

### Retry Logic

If a DHCP phase times out, the client retries with exponential backoff:

- Initial timeout: 2 seconds
- Maximum timeout: 32 seconds
- Maximum retries: 5 per phase
- If all retries fail, the network subsystem reports failure to the AI bootstrap

## TLS 1.2

**Library**: BearSSL (`programs/netstack/bearssl/`)

TLS provides encrypted and authenticated communication for all marketplace and API traffic:

- **Protocol version**: TLS 1.2 (maximum supported by BearSSL; TLS 1.0 and 1.1 disabled)
- **Cipher suites**: AES-128-GCM, AES-256-GCM, ChaCha20-Poly1305
- **Key exchange**: X25519 ECDHE
- **Certificate validation**: X.509 certificate chain verification against embedded root CAs
- **Embedded root CAs**: ISRG Root X1, Amazon Root CA 1, DigiCert Global Root G2
- **Certificate pinning**: The marketplace domain uses pinned intermediate CA certificates

### Why BearSSL

BearSSL is chosen for its suitability in bare-metal environments:
- Small code size (~75 KB compiled)
- No dynamic memory allocation (uses caller-provided buffers)
- Constant-time implementations (side-channel resistant)
- No external dependencies (no libc, no OS calls)

## HTTP/1.1

The HTTP layer provides request/response communication over TLS:

- **Methods**: GET, POST (used by marketplace API and LLM backends)
- **Chunked transfer encoding**: Supports chunked responses for streaming
- **Content-Length**: Standard fixed-length responses
- **Headers**: Host, Content-Type, Authorization, User-Agent
- **Keep-alive**: Connection reuse for multiple requests to the same host

### Example: Marketplace API Call

```
POST /v1/manifest HTTP/1.1
Host: api.aljefra.com
Content-Type: application/json
Content-Length: 342

{"arch":"x86_64","cpu":"Intel Core i7","ram_mb":32768,...}
```

## WiFi Framework

**Source**: `drivers/network/wifi_framework.c`

The WiFi framework provides a common interface for WiFi NIC drivers:

### Operations

```c
int wifi_scan(wifi_network_t *results, int max_results);
int wifi_connect(const char *ssid, const char *password);
int wifi_disconnect(void);
int wifi_status(wifi_status_t *status);
```

### Security

- **WPA2-Personal**: AES-CCMP encryption with pre-shared key
- **4-way handshake**: Standard WPA2 key exchange
- **Key derivation**: PBKDF2-SHA1 for PSK generation from passphrase

### Scan Results

```c
typedef struct {
    char     ssid[33];        // Network name (max 32 chars + NUL)
    uint8_t  bssid[6];        // Access point MAC address
    int8_t   rssi;            // Signal strength in dBm
    uint8_t  channel;         // WiFi channel number
    uint8_t  security;        // WIFI_SEC_OPEN, WIFI_SEC_WPA2, etc.
} wifi_network_t;
```

## Checksum Helpers

**Source**: `net/checksum.h`

IP and TCP checksums use the standard one's complement algorithm:

```c
/**
 * Compute the IP header checksum.
 * @param header  Pointer to the IP header (20+ bytes)
 * @return        16-bit checksum in network byte order
 */
uint16_t ip_checksum(const void *header, size_t len);

/**
 * Compute the TCP checksum including the pseudo-header.
 * @param src_ip   Source IPv4 address
 * @param dst_ip   Destination IPv4 address
 * @param tcp_seg  Pointer to the TCP segment (header + data)
 * @param tcp_len  Total length of the TCP segment
 * @return         16-bit checksum in network byte order
 */
uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip,
                      const void *tcp_seg, size_t tcp_len);
```

## Network Configuration

The network stack is designed for zero-configuration operation:

1. **NIC detection**: PCIe bus scan identifies the network adapter
2. **Driver loading**: Built-in driver initializes the NIC, or marketplace driver is downloaded via a working NIC
3. **DHCP**: Automatic IP configuration with no manual setup
4. **DNS**: Server address obtained from DHCP, no manual DNS entry
5. **Routing**: Default gateway from DHCP, single-homed routing

No manual IP configuration is required or supported. The system is fully automatic, consistent with the AI-first design philosophy.
