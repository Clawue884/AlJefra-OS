# AlJefra Driver Marketplace Server

REST API server that AlJefra OS devices connect to for downloading drivers.

## Quick Start

```bash
cd server
pip install -r requirements.txt
python app.py
```

The server starts on `http://0.0.0.0:8080` and auto-seeds stub drivers for
common QEMU devices on first run.

## Endpoints

| Method | Path | Description |
|--------|------|-------------|
| GET | `/` | Health check / API info |
| GET | `/v1/catalog` | List all available drivers |
| POST | `/v1/manifest` | Send hardware manifest, get recommendations |
| GET | `/v1/drivers/<vendor>/<device>/<arch>` | Download .ajdrv binary |
| POST | `/v1/drivers` | Upload new driver package |
| GET | `/v1/updates/<os_version>` | Check for OS/driver updates |

## Testing

```bash
# List all drivers
curl http://localhost:8080/v1/catalog

# Filter by architecture
curl http://localhost:8080/v1/catalog?arch=x86_64

# Filter by category
curl http://localhost:8080/v1/catalog?category=network

# Send a hardware manifest (like the OS client does)
curl -X POST http://localhost:8080/v1/manifest \
  -H 'Content-Type: application/json' \
  -d '{
    "arch": "x86_64",
    "cpu_vendor": "GenuineIntel",
    "cpu_model": "QEMU Virtual CPU",
    "ram_mb": 256,
    "devices": [
      {"v": "8086", "d": "10d3", "c": 2, "s": 0, "has_drv": false},
      {"v": "1af4", "d": "1001", "c": 1, "s": 0, "has_drv": false},
      {"v": "1234", "d": "1111", "c": 3, "s": 0, "has_drv": true}
    ]
  }'

# Download a specific driver
curl http://localhost:8080/v1/drivers/8086/10d3/x86_64 -o e1000.ajdrv

# Check for OS updates
curl http://localhost:8080/v1/updates/0.9.0
```

## Docker Deployment

```bash
docker compose up -d
```

## Building .ajdrv Packages

The `ajdrv_builder.py` tool creates .ajdrv packages per the binary format
defined in `store/package.h`.

```bash
# Generate Ed25519 test signing keys
python ajdrv_builder.py generate-keys --dir keys

# Build a driver package from compiled binary
python ajdrv_builder.py build \
  --name my_driver --desc "My custom driver" \
  --vendor 1234 --device 5678 --arch x86_64 \
  --category network --code compiled.bin \
  --key keys/signing.key --out drivers/x86_64/1234_5678.ajdrv

# Re-seed all stub drivers
python ajdrv_builder.py seed
```

## Seed Drivers

The following stub drivers are auto-generated:

| Driver | Vendor:Device | Architectures | Category |
|--------|--------------|---------------|----------|
| virtio_blk | 1AF4:1001 | x86_64, aarch64, riscv64 | storage |
| virtio_net | 1AF4:1000 | x86_64, aarch64, riscv64 | network |
| e1000 | 8086:10D3 | x86_64 | network |
| qemu_vga | 1234:1111 | x86_64 | display |

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `ALJEFRA_HOST` | `0.0.0.0` | Bind address |
| `ALJEFRA_PORT` | `8080` | Listen port |
| `ALJEFRA_DEBUG` | `0` | Set to `1` for Flask debug mode |

## File Layout

```
server/
  app.py              -- Flask REST API application
  models.py           -- Data models (DriverMeta, HardwareManifest, etc.)
  driver_store.py     -- File-based driver storage backend
  ajdrv_builder.py    -- .ajdrv package builder + seed tool
  requirements.txt    -- Python dependencies
  Dockerfile          -- Container image
  docker-compose.yml  -- Docker Compose deployment
  catalog.json        -- Driver catalog (auto-generated)
  drivers/            -- .ajdrv binary storage
    x86_64/
    aarch64/
    riscv64/
```
