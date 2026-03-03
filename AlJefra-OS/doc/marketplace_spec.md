# AlJefra OS Marketplace API Specification

## Overview

The AlJefra Marketplace is a centralized driver distribution and OS update platform for
AlJefra OS. It provides hardware-aware driver recommendations, package downloads, and
over-the-air (OTA) update delivery. The server is implemented as a Python Flask REST API
and communicates exclusively over HTTPS in production, with Ed25519 signature verification
enforced on all driver packages.

---

## Server Details

| Property       | Value                                      |
|----------------|--------------------------------------------|
| Framework      | Python 3 / Flask                           |
| Entry point    | `server/app.py`                            |
| Production URL | `https://store.aljefra.com`                |
| Local dev URL  | `http://localhost:8081`                     |
| Transport      | HTTPS (TLS 1.2 via BearSSL in production)  |
| Auth model     | Ed25519 public-key signatures on packages  |
| Content types  | `application/json` (metadata), `application/octet-stream` (packages) |

---

## API Endpoints

All endpoints are versioned under `/v1`. Request and response bodies use JSON unless
otherwise noted. HTTP status codes follow standard REST conventions.

### POST /v1/manifest

Send a hardware manifest describing the target machine. The server returns a prioritized
list of driver recommendations for every detected device.

**Request**

```http
POST /v1/manifest HTTP/1.1
Content-Type: application/json
```

```json
{
  "arch": "x86_64",
  "devices": [
    {
      "type": "pci",
      "vendor_id": "0x8086",
      "device_id": "0x1234"
    },
    {
      "type": "pci",
      "vendor_id": "0x1AF4",
      "device_id": "0x1000"
    }
  ],
  "os_version": "1.0"
}
```

| Field            | Type     | Required | Description                                      |
|------------------|----------|----------|--------------------------------------------------|
| `arch`           | string   | yes      | Target architecture: `x86_64`, `aarch64`, `riscv64` |
| `devices`        | array    | yes      | Array of device descriptors                      |
| `devices[].type` | string   | yes      | Bus type, currently only `pci`                   |
| `devices[].vendor_id` | string | yes   | PCI vendor ID in hex (e.g. `"0x8086"`)           |
| `devices[].device_id` | string | yes   | PCI device ID in hex (e.g. `"0x1234"`)           |
| `os_version`     | string   | yes      | Installed AlJefra OS version for compatibility   |

**Response (200 OK)**

```json
{
  "recommendations": [
    {
      "driver": "intel-e1000",
      "version": "2.1.0",
      "url": "/v1/drivers/8086/1234/x86_64",
      "priority": "critical",
      "category": "network",
      "size_bytes": 8192,
      "sha256": "a1b2c3..."
    },
    {
      "driver": "virtio-net",
      "version": "1.0.0",
      "url": "/v1/drivers/1AF4/1000/x86_64",
      "priority": "recommended",
      "category": "network",
      "size_bytes": 6144,
      "sha256": "d4e5f6..."
    }
  ]
}
```

| Field                        | Type   | Description                                          |
|------------------------------|--------|------------------------------------------------------|
| `recommendations`            | array  | Ordered list of driver recommendations               |
| `recommendations[].driver`   | string | Human-readable driver name                           |
| `recommendations[].version`  | string | Semantic version of the driver package               |
| `recommendations[].url`      | string | Relative URL to download the `.ajdrv` package        |
| `recommendations[].priority` | string | `critical`, `recommended`, or `optional`             |
| `recommendations[].category` | string | Driver category (see categories below)               |
| `recommendations[].size_bytes` | int  | Package size in bytes                                |
| `recommendations[].sha256`   | string | SHA-256 hash of the package for integrity checking   |

**Priority Levels**

| Priority      | Meaning                                                      |
|---------------|--------------------------------------------------------------|
| `critical`    | Required for basic system operation (boot disk, console)     |
| `recommended` | Strongly suggested for full functionality (network, display) |
| `optional`    | Nice to have; non-essential peripherals                      |

**Error Responses**

| Status | Condition                            |
|--------|--------------------------------------|
| 400    | Malformed manifest or missing fields |
| 422    | Unsupported architecture             |
| 500    | Internal server error                |

---

### GET /v1/catalog

List all available drivers in the marketplace. Supports optional query parameters for
filtering.

**Request**

```http
GET /v1/catalog HTTP/1.1
GET /v1/catalog?arch=x86_64&category=network HTTP/1.1
```

| Parameter  | Type   | Required | Description                          |
|------------|--------|----------|--------------------------------------|
| `arch`     | string | no       | Filter by architecture               |
| `category` | string | no       | Filter by driver category            |
| `vendor`   | string | no       | Filter by PCI vendor ID (hex, no prefix) |

**Response (200 OK)**

```json
[
  {
    "name": "virtio-blk",
    "version": "1.0.0",
    "vendor_id": "1AF4",
    "device_id": "1001",
    "arch": ["x86_64", "aarch64", "riscv64"],
    "category": "storage",
    "description": "VirtIO block storage driver",
    "size_bytes": 7168,
    "published": "2025-01-15T00:00:00Z",
    "publisher": "aljefra-foundation"
  },
  {
    "name": "e1000",
    "version": "2.1.0",
    "vendor_id": "8086",
    "device_id": "10D3",
    "arch": ["x86_64"],
    "category": "network",
    "description": "Intel E1000 Gigabit Ethernet driver",
    "size_bytes": 8192,
    "published": "2025-02-10T00:00:00Z",
    "publisher": "aljefra-foundation"
  }
]
```

---

### GET /v1/drivers/{vendor}/{device}/{arch}

Download a signed `.ajdrv` driver package for specific hardware on a specific
architecture.

**Request**

```http
GET /v1/drivers/8086/10D3/x86_64 HTTP/1.1
```

| Path Parameter | Description                               |
|----------------|-------------------------------------------|
| `vendor`       | PCI vendor ID in hex (e.g. `8086`)        |
| `device`       | PCI device ID in hex (e.g. `10D3`)        |
| `arch`         | Target architecture (`x86_64`, `aarch64`, `riscv64`) |

**Response (200 OK)**

Returns the raw `.ajdrv` binary package with `Content-Type: application/octet-stream`.

**Error Responses**

| Status | Condition                                      |
|--------|------------------------------------------------|
| 404    | No driver found for the given vendor/device/arch combination |
| 410    | Driver has been revoked                        |

---

### GET /v1/updates/{version}

Check for available OS updates newer than the specified version.

**Request**

```http
GET /v1/updates/1.0 HTTP/1.1
```

| Path Parameter | Description                        |
|----------------|------------------------------------|
| `version`      | Currently installed OS version     |

**Response (200 OK)**

```json
{
  "available": true,
  "latest_version": "1.1.0",
  "release_date": "2025-03-01T00:00:00Z",
  "changelog": "Bug fixes and new RISC-V SMP support.",
  "download_url": "/v1/updates/download/1.1.0",
  "size_bytes": 524288,
  "sha256": "abc123...",
  "signature": "<base64-encoded Ed25519 signature>",
  "min_version": "0.9.0"
}
```

**Response (200 OK, no update)**

```json
{
  "available": false,
  "latest_version": "1.0.0"
}
```

---

### POST /v1/drivers

Upload a new driver package to the marketplace. The package must be a valid `.ajdrv` file
with a correct Ed25519 signature from a registered publisher.

**Request**

```http
POST /v1/drivers HTTP/1.1
Content-Type: multipart/form-data

--boundary
Content-Disposition: form-data; name="package"; filename="virtio_blk.ajdrv"
Content-Type: application/octet-stream

<binary .ajdrv data>
--boundary
Content-Disposition: form-data; name="publisher_key"

<base64-encoded Ed25519 public key>
--boundary--
```

**Response (201 Created)**

```json
{
  "status": "accepted",
  "driver": "virtio-blk",
  "version": "1.0.0",
  "review_id": "rv-20250301-001"
}
```

**Error Responses**

| Status | Condition                                              |
|--------|--------------------------------------------------------|
| 400    | Invalid `.ajdrv` format or missing fields              |
| 401    | Unknown publisher key                                  |
| 403    | Signature verification failed                          |
| 409    | Driver version already exists                          |
| 413    | Package exceeds maximum size (1 MB)                    |

---

## .ajdrv Package Format

The `.ajdrv` format is the standard binary package for AlJefra OS drivers. Every package
consists of three regions: a fixed-size header, variable-length metadata and code, and a
trailing Ed25519 signature.

### Layout

```
+------------------------------------------------------+
|  Header (64 bytes)                                    |
+------------------------------------------------------+
|  Metadata section (variable length)                   |
|  - Driver name (null-terminated string)               |
|  - Driver description (null-terminated string)        |
+------------------------------------------------------+
|  Binary code section (variable length)                |
|  - Compiled driver object code                        |
|  - Entry point at header-specified offset             |
+------------------------------------------------------+
|  Ed25519 Signature (64 bytes)                         |
+------------------------------------------------------+
```

### Header Structure (64 bytes)

```c
struct ajdrv_header {
    uint32_t magic;           // 0x56444A41 ("AJDV" in little-endian)
    uint16_t version_major;   // Package format version (major)
    uint16_t version_minor;   // Package format version (minor)
    uint8_t  arch;            // Architecture code (see table below)
    uint8_t  category;        // Driver category code (see table below)
    uint8_t  flags;           // Feature flags (bit 0: compressed, bit 1: debuggable)
    uint8_t  reserved;        // Must be zero
    uint32_t code_offset;     // Byte offset from start of file to code section
    uint32_t code_size;       // Size of code section in bytes
    uint32_t entry_offset;    // Offset within code section to driver entry point
    uint32_t signature_offset;// Byte offset from start of file to Ed25519 signature
    uint16_t vendor_id;       // PCI vendor ID
    uint16_t device_id;       // PCI device ID
    uint16_t min_os_major;    // Minimum required OS version (major)
    uint16_t min_os_minor;    // Minimum required OS version (minor)
    char     name[16];        // Null-terminated driver short name
    char     desc[8];         // Null-terminated short description tag
};
```

### Magic Number

The magic number `0x56444A41` corresponds to the ASCII string `"AJDV"` when read as a
little-endian 32-bit integer. All `.ajdrv` files must begin with these four bytes. The
kernel loader rejects any file that does not match this magic.

### Architecture Codes

| Code | Architecture | Description                         |
|------|-------------|--------------------------------------|
| 0    | x86_64      | AMD64 / Intel 64-bit                 |
| 1    | aarch64     | ARM 64-bit (ARMv8-A and later)       |
| 2    | riscv64     | RISC-V 64-bit (RV64GC)              |
| 0xFF | any         | Architecture-independent (data)      |

### Driver Category Codes

| Code | Category | Description                                |
|------|----------|--------------------------------------------|
| 0    | storage  | Block devices, disk controllers, NVMe      |
| 1    | network  | Ethernet, WiFi, virtual NICs               |
| 2    | input    | Keyboard, mouse, touchpad, gamepad         |
| 3    | display  | GPU, framebuffer, VGA                      |
| 4    | gpu      | Dedicated GPU drivers                      |
| 5    | bus      | PCI, USB, I2C, SPI host controllers        |
| 6    | other    | Miscellaneous drivers                      |

### Flags

| Bit | Name        | Description                                    |
|-----|-------------|------------------------------------------------|
| 0   | COMPRESSED  | Code section is LZ4-compressed                 |
| 1   | DEBUGGABLE  | Package includes debug symbols                 |
| 2-7 | Reserved   | Must be zero                                   |

### Signature

The final 64 bytes of every `.ajdrv` file contain an Ed25519 signature computed over all
preceding bytes (header + metadata + code). The kernel verifies this signature against the
publisher's public key before loading the driver into memory.

---

## Seed Drivers

AlJefra OS ships with built-in support for essential virtual hardware. These seed drivers
are compiled into the kernel or available as first-party packages in the marketplace.

| Driver      | Vendor ID | Device ID | Category | Description                    |
|-------------|-----------|-----------|----------|--------------------------------|
| virtio_blk  | 1AF4      | 1001      | storage  | VirtIO block storage           |
| virtio_net  | 1AF4      | 1000      | network  | VirtIO network interface       |
| e1000       | 8086      | 10D3      | network  | Intel E1000 Gigabit Ethernet   |
| qemu_vga    | 1234      | 1111      | display  | QEMU standard VGA              |

These drivers are always available even without marketplace connectivity, ensuring the
system can boot and establish a network connection for fetching additional drivers.

---

## Deployment

### Docker Compose (Recommended)

The marketplace server ships with a `docker-compose.yml` for containerized deployment.

```yaml
# server/docker-compose.yml
version: "3.8"
services:
  marketplace:
    build: .
    ports:
      - "8081:8081"
    volumes:
      - ./drivers:/app/drivers
      - ./keys:/app/keys
    environment:
      - FLASK_ENV=production
      - ALJEFRA_STORE_PORT=8081
      - ALJEFRA_KEY_DIR=/app/keys
    restart: unless-stopped
```

**Start the server:**

```bash
cd server
docker-compose up -d
```

**View logs:**

```bash
docker-compose logs -f marketplace
```

**Stop the server:**

```bash
docker-compose down
```

### Local Development

For development without Docker:

```bash
cd server
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt
python3 app.py
```

The server starts on `http://localhost:8081` by default. Set the `ALJEFRA_STORE_PORT`
environment variable to change the port.

---

## Building .ajdrv Packages

The `ajdrv_builder.py` tool compiles driver source code into signed `.ajdrv` packages.

### Usage

```bash
python3 tools/ajdrv_builder.py \
    --name "my-driver" \
    --arch x86_64 \
    --category network \
    --vendor-id 0x1234 \
    --device-id 0x5678 \
    --source drivers/network/my_driver.c \
    --key keys/publisher_private.pem \
    --output my_driver.ajdrv
```

### Options

| Flag            | Description                                     |
|-----------------|-------------------------------------------------|
| `--name`        | Driver short name (max 15 chars)                |
| `--arch`        | Target architecture (`x86_64`, `aarch64`, `riscv64`) |
| `--category`    | Driver category (`storage`, `network`, `input`, `display`, `bus`) |
| `--vendor-id`   | PCI vendor ID in hex                            |
| `--device-id`   | PCI device ID in hex                            |
| `--source`      | Path to driver C source file                    |
| `--key`         | Path to Ed25519 private key (PEM format)        |
| `--output`      | Output `.ajdrv` file path                       |
| `--min-os`      | Minimum OS version (default: `1.0`)             |
| `--compress`    | Enable LZ4 compression of code section          |
| `--debug`       | Include debug symbols                           |

### Key Generation

Generate a new Ed25519 key pair for signing packages:

```bash
python3 tools/ajdrv_builder.py --genkey \
    --key-out keys/publisher_private.pem \
    --pubkey-out keys/publisher_public.pem
```

### Verification

Verify an existing package without loading it:

```bash
python3 tools/ajdrv_builder.py --verify \
    --input my_driver.ajdrv \
    --pubkey keys/publisher_public.pem
```

---

## Client-Side Integration

The AlJefra OS kernel integrates with the marketplace through the following workflow:

1. **Boot**: The kernel enumerates PCI devices and builds a hardware manifest.
2. **Connect**: If a network driver is available (seed drivers), the kernel connects to
   `store.aljefra.com` over TLS 1.2.
3. **Submit manifest**: The kernel sends `POST /v1/manifest` with the detected hardware.
4. **Download**: Critical and recommended drivers are downloaded via
   `GET /v1/drivers/{vendor}/{device}/{arch}`.
5. **Verify**: Each `.ajdrv` package is verified against the embedded root public key
   (see `security_model.md`).
6. **Load**: Verified drivers are loaded into kernel memory and their entry points are
   called to initialize hardware.
7. **Update check**: Periodically, the kernel queries `GET /v1/updates/{version}` to
   check for OS-level updates.

---

## Rate Limiting

The production server enforces the following rate limits:

| Endpoint          | Limit              |
|-------------------|--------------------|
| POST /v1/manifest | 60 requests/minute |
| GET /v1/catalog   | 120 requests/minute|
| GET /v1/drivers/* | 30 requests/minute |
| POST /v1/drivers  | 5 requests/hour    |

Exceeding these limits returns HTTP 429 with a `Retry-After` header.

---

## Versioning

The API is versioned in the URL path (`/v1/`). Breaking changes will increment the
version number. The server supports at most two API versions simultaneously to allow
clients time to migrate.
