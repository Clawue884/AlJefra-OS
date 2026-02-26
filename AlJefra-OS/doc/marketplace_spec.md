# AlJefra OS — Driver Marketplace API Specification

## Base URL

```
https://api.aljefra.com/v1
```

## Authentication

Boot-time requests use a device certificate embedded in the OS image. Third-party submissions use API keys.

## Endpoints

### GET /catalog

List all available drivers.

**Query Parameters:**
- `arch` — Filter by architecture: `x86_64`, `aarch64`, `riscv64`
- `category` — Filter by category: `storage`, `network`, `input`, `display`, `gpu`, `bus`
- `page`, `per_page` — Pagination

**Response:**
```json
{
  "drivers": [
    {
      "name": "nvme",
      "version": "1.0.0",
      "vendor_id": "0x144d",
      "device_id": "0xa808",
      "arch": "x86_64",
      "category": "storage",
      "size_bytes": 45056,
      "sha256": "abcd1234...",
      "download_url": "/v1/drivers/144d/a808/x86_64"
    }
  ],
  "total": 42,
  "page": 1
}
```

### POST /manifest

Send a hardware manifest, receive driver recommendations.

**Request Body:**
```json
{
  "arch": "x86_64",
  "cpu_vendor": "GenuineIntel",
  "cpu_model": "Intel Core i7-12700K",
  "ram_mb": 32768,
  "devices": [
    {"v": "8086", "d": "15f3", "c": 2, "s": 0, "has_drv": false},
    {"v": "144d", "d": "a808", "c": 1, "s": 8, "has_drv": false},
    {"v": "8086", "d": "a0a3", "c": 12, "s": 3, "has_drv": false}
  ]
}
```

**Response:**
```json
{
  "recommendations": [
    {
      "vendor_id": "8086",
      "device_id": "15f3",
      "driver_name": "intel_i225",
      "version": "1.0.0",
      "priority": "critical",
      "download_url": "/v1/drivers/8086/15f3/x86_64"
    }
  ],
  "os_update_available": false
}
```

### GET /drivers/{vendor}/{device}/{arch}

Download a signed `.ajdrv` driver package.

**Response:** Binary `.ajdrv` file with `Content-Type: application/octet-stream`.

### POST /drivers

Submit a new driver (requires API key).

**Request:** Multipart form with `.ajdrv` file and metadata JSON.

### GET /updates/{os_version}

Check for OS updates.

**Response:**
```json
{
  "update_available": true,
  "version": "2.0.0",
  "changelog": "Multi-arch support, DHCP, ...",
  "download_url": "/v1/updates/2.0.0/x86_64"
}
```

## .ajdrv Package Format

See `store/package.h` for the binary format specification.

```
Offset  Size    Field
0x00    4       Magic: "AJDV" (0x56444A41)
0x04    4       Format version
0x08    4       Architecture (0=x86_64, 1=aarch64, 2=riscv64)
0x0C    4       Category
0x10    4       Code offset
0x14    4       Code size
0x18    4       Name offset
0x1C    4       Name size
0x20    4       Description offset
0x24    4       Description size
0x28    4       Entry point offset (within code)
0x2C    4       Signature offset
0x30    2       Vendor ID
0x32    2       Device ID
0x34    2       Min OS version
0x36    2       Flags
0x38    8       Reserved
[name]          Driver name (null-terminated)
[desc]          Description (null-terminated)
[code]          Relocatable binary
[sig]   64      Ed25519 signature
```

## Security

- All packages are signed with Ed25519
- The OS ships with the AlJefra Store public key
- Signature covers header + name + description + code
- HTTPS (TLS 1.2+) for all API communication
- Certificate pinning for the marketplace domain
