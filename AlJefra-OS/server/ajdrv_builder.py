#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# AlJefra OS -- .ajdrv Package Builder
#
# Creates signed .ajdrv driver packages per the binary format defined in
# store/package.h.
#
# Usage:
#   python ajdrv_builder.py --name virtio_blk \
#       --vendor 1af4 --device 1001 --arch x86_64 \
#       --category storage --code stub.bin \
#       --key keys/signing.key --out drivers/x86_64/1af4_1001.ajdrv
#
#   python ajdrv_builder.py --generate-keys   # create test keypair

"""
.ajdrv binary layout (from store/package.h):

    Offset  Size  Field
    0x00    4     Magic: "AJDV" (0x56444A41 LE)
    0x04    4     Format version (1)
    0x08    4     Architecture (0=x86_64, 1=aarch64, 2=riscv64, 0xFF=any)
    0x0C    4     Category (0=storage, 1=network, ...)
    0x10    4     Code offset
    0x14    4     Code size
    0x18    4     Name offset
    0x1C    4     Name size (including null)
    0x20    4     Description offset
    0x24    4     Description size
    0x28    4     Entry point offset (within code section)
    0x2C    4     Signature offset
    0x30    2     Vendor ID
    0x32    2     Device ID
    0x34    2     Min OS version (packed major.minor)
    0x36    2     Flags
    0x38    8     Reserved
    ---     ---   64 bytes header total
    [name]        Null-terminated driver name
    [desc]        Null-terminated description
    [code]        Relocatable binary
    [sig]   64    Ed25519 signature over [0..signature_offset)
"""

import argparse
import hashlib
import os
import struct
import sys
from pathlib import Path

# Ed25519 via PyNaCl (pure Python fallback: nacl.signing)
try:
    from nacl.signing import SigningKey, VerifyKey
    from nacl.encoding import RawEncoder
    HAS_NACL = True
except ImportError:
    HAS_NACL = False

# ── Paths ──

BASE_DIR = Path(__file__).resolve().parent
DRIVERS_DIR = BASE_DIR / "drivers"
CATALOG_FILE = BASE_DIR / "catalog.json"

# ── Constants (must match store/package.h) ──

AJDRV_MAGIC   = 0x56444A41  # "AJDV" in LE
AJDRV_VERSION = 1
HEADER_SIZE   = 64  # 0x40 bytes
SIG_SIZE      = 64  # Ed25519 signature

ARCH_MAP = {
    "x86_64":  0,
    "aarch64": 1,
    "riscv64": 2,
    "any":     0xFF,
}

CATEGORY_MAP = {
    "storage": 0,
    "network": 1,
    "input":   2,
    "display": 3,
    "gpu":     4,
    "bus":     5,
    "other":   6,
}


def _pack_version(ver_str: str) -> int:
    """Pack "major.minor" into uint16: (major << 8) | minor."""
    parts = ver_str.split(".")
    major = int(parts[0]) if len(parts) > 0 else 1
    minor = int(parts[1]) if len(parts) > 1 else 0
    return ((major & 0xFF) << 8) | (minor & 0xFF)


def generate_keys(key_dir: str) -> None:
    """Generate an Ed25519 signing keypair for testing."""
    if not HAS_NACL:
        print("ERROR: PyNaCl is required for key generation.")
        print("       Install with: pip install pynacl")
        sys.exit(1)

    key_path = Path(key_dir)
    key_path.mkdir(parents=True, exist_ok=True)

    sk = SigningKey.generate()
    vk = sk.verify_key

    sk_file = key_path / "signing.key"
    vk_file = key_path / "verify.key"

    with open(sk_file, "wb") as f:
        f.write(bytes(sk))
    with open(vk_file, "wb") as f:
        f.write(bytes(vk))

    # Also write a C header with the public key for embedding in the OS
    vk_bytes = bytes(vk)
    c_array = ", ".join(f"0x{b:02x}" for b in vk_bytes)
    c_header = key_path / "pubkey.h"
    with open(c_header, "w") as f:
        f.write("/* Auto-generated AlJefra Store public key */\n")
        f.write("#ifndef ALJEFRA_STORE_PUBKEY_H\n")
        f.write("#define ALJEFRA_STORE_PUBKEY_H\n\n")
        f.write("static const uint8_t aljefra_store_pubkey[32] = {\n")
        f.write(f"    {c_array}\n")
        f.write("};\n\n")
        f.write("#endif /* ALJEFRA_STORE_PUBKEY_H */\n")

    print(f"Signing key:  {sk_file}")
    print(f"Verify key:   {vk_file}")
    print(f"C header:     {c_header}")
    print(f"Public key:   {vk_bytes.hex()}")


def build_ajdrv(
    name: str,
    description: str,
    vendor_id: int,
    device_id: int,
    arch: str,
    category: str,
    code: bytes,
    signing_key_path: str = "",
    entry_offset: int = 0,
    min_os_version: str = "1.0",
    flags: int = 0,
) -> bytes:
    """Build a complete .ajdrv binary package.

    Returns the raw bytes of the package.
    """
    arch_code = ARCH_MAP.get(arch, 0)
    cat_code = CATEGORY_MAP.get(category, 6)

    # Prepare name and description as null-terminated bytes
    name_bytes = (name + "\0").encode("utf-8")
    desc_bytes = (description + "\0").encode("utf-8")

    # Calculate offsets
    # Layout: [header 64] [name] [desc] [code] [signature 64]
    name_offset = HEADER_SIZE
    desc_offset = name_offset + len(name_bytes)
    code_offset = desc_offset + len(desc_bytes)
    sig_offset = code_offset + len(code)

    os_ver = _pack_version(min_os_version)

    # Pack header (64 bytes, all little-endian)
    header = struct.pack(
        "<"        # little-endian
        "I"        # 0x00: magic
        "I"        # 0x04: version
        "I"        # 0x08: arch
        "I"        # 0x0C: category
        "I"        # 0x10: code_offset
        "I"        # 0x14: code_size
        "I"        # 0x18: name_offset
        "I"        # 0x1C: name_size
        "I"        # 0x20: desc_offset
        "I"        # 0x24: desc_size
        "I"        # 0x28: entry_offset
        "I"        # 0x2C: signature_offset
        "H"        # 0x30: vendor_id
        "H"        # 0x32: device_id
        "H"        # 0x34: min_os_version
        "H"        # 0x36: flags
        "8s",      # 0x38: reserved (8 bytes)
        AJDRV_MAGIC,
        AJDRV_VERSION,
        arch_code,
        cat_code,
        code_offset,
        len(code),
        name_offset,
        len(name_bytes),
        desc_offset,
        len(desc_bytes),
        entry_offset,
        sig_offset,
        vendor_id,
        device_id,
        os_ver,
        flags,
        b"\x00" * 8,
    )

    assert len(header) == HEADER_SIZE, f"Header is {len(header)} bytes, expected {HEADER_SIZE}"

    # Assemble the signed region: header + name + desc + code
    signed_data = header + name_bytes + desc_bytes + code

    # Sign with Ed25519
    signature = b"\x00" * SIG_SIZE  # default: zero signature
    if signing_key_path and HAS_NACL:
        with open(signing_key_path, "rb") as f:
            sk_bytes = f.read()
        sk = SigningKey(sk_bytes)
        signed_msg = sk.sign(signed_data, encoder=RawEncoder)
        signature = signed_msg.signature
        assert len(signature) == SIG_SIZE

    package = signed_data + signature
    return package


def create_stub_code(arch: str) -> bytes:
    """Create a minimal stub driver binary for testing.

    For x86_64: a simple RET instruction.
    For aarch64: RET instruction encoding.
    For riscv64: RET instruction encoding.
    """
    if arch == "x86_64":
        # push rbp; mov rbp, rsp; xor eax, eax; pop rbp; ret
        return bytes([
            0x55,                   # push rbp
            0x48, 0x89, 0xE5,      # mov rbp, rsp
            0x31, 0xC0,            # xor eax, eax
            0x5D,                   # pop rbp
            0xC3,                   # ret
        ])
    elif arch == "aarch64":
        # mov x0, #0; ret
        return struct.pack("<II",
            0xD2800000,  # mov x0, #0
            0xD65F03C0,  # ret
        )
    elif arch == "riscv64":
        # li a0, 0; ret
        return struct.pack("<II",
            0x00000513,  # addi a0, zero, 0
            0x00008067,  # ret (jalr zero, ra, 0)
        )
    else:
        return b"\x00" * 8


def main():
    parser = argparse.ArgumentParser(
        description="Build .ajdrv driver packages for AlJefra OS"
    )
    sub = parser.add_subparsers(dest="command")

    # -- generate-keys --
    keygen = sub.add_parser("generate-keys", help="Generate Ed25519 test keypair")
    keygen.add_argument("--dir", default="keys", help="Output directory")

    # -- build --
    build = sub.add_parser("build", help="Build a .ajdrv package")
    build.add_argument("--name", required=True, help="Driver name")
    build.add_argument("--desc", default="", help="Driver description")
    build.add_argument("--vendor", required=True, help="PCI vendor ID (hex)")
    build.add_argument("--device", required=True, help="PCI device ID (hex)")
    build.add_argument("--arch", required=True, choices=list(ARCH_MAP.keys()))
    build.add_argument("--category", required=True, choices=list(CATEGORY_MAP.keys()))
    build.add_argument("--code", default="", help="Path to compiled binary (or omit for stub)")
    build.add_argument("--key", default="", help="Path to Ed25519 signing key")
    build.add_argument("--out", required=True, help="Output .ajdrv path")
    build.add_argument("--entry-offset", type=int, default=0)
    build.add_argument("--min-os", default="1.0")
    build.add_argument("--flags", type=int, default=0)

    # -- seed --
    seed = sub.add_parser("seed", help="Generate seed catalog with stub drivers")
    seed.add_argument("--key", default="", help="Signing key (optional)")

    args = parser.parse_args()

    if args.command == "generate-keys":
        generate_keys(args.dir)

    elif args.command == "build":
        # Load code
        if args.code:
            with open(args.code, "rb") as f:
                code = f.read()
        else:
            code = create_stub_code(args.arch)

        vendor = int(args.vendor, 16)
        device = int(args.device, 16)

        package = build_ajdrv(
            name=args.name,
            description=args.desc or f"{args.name} driver for AlJefra OS",
            vendor_id=vendor,
            device_id=device,
            arch=args.arch,
            category=args.category,
            code=code,
            signing_key_path=args.key,
            entry_offset=args.entry_offset,
            min_os_version=args.min_os,
            flags=args.flags,
        )

        out_path = Path(args.out)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        with open(out_path, "wb") as f:
            f.write(package)

        sha = hashlib.sha256(package).hexdigest()
        print(f"Built: {out_path} ({len(package)} bytes, SHA-256: {sha[:16]}...)")

    elif args.command == "seed":
        seed_catalog(args.key)

    else:
        parser.print_help()


# ── Seed data for QEMU testing ──

SEED_DRIVERS = [
    # VirtIO Block (1AF4:1001) -- all architectures
    {
        "name": "virtio_blk",
        "desc": "VirtIO block device driver",
        "vendor": 0x1AF4,
        "device": 0x1001,
        "category": "storage",
        "archs": ["x86_64", "aarch64", "riscv64"],
    },
    # VirtIO Net (1AF4:1000) -- all architectures
    {
        "name": "virtio_net",
        "desc": "VirtIO network device driver",
        "vendor": 0x1AF4,
        "device": 0x1000,
        "category": "network",
        "archs": ["x86_64", "aarch64", "riscv64"],
    },
    # Intel e1000 (8086:10D3) -- x86_64 only
    {
        "name": "e1000",
        "desc": "Intel e1000/e1000e Gigabit Ethernet driver",
        "vendor": 0x8086,
        "device": 0x10D3,
        "category": "network",
        "archs": ["x86_64"],
    },
    # QEMU VGA (1234:1111) -- x86_64 only
    {
        "name": "qemu_vga",
        "desc": "QEMU Standard VGA (Bochs VBE) display driver",
        "vendor": 0x1234,
        "device": 0x1111,
        "category": "display",
        "archs": ["x86_64"],
    },
]


def seed_catalog(signing_key: str = "") -> None:
    """Generate stub .ajdrv files and catalog.json for testing."""
    import json

    catalog_entries = []

    for drv in SEED_DRIVERS:
        for arch in drv["archs"]:
            code = create_stub_code(arch)

            package = build_ajdrv(
                name=drv["name"],
                description=drv["desc"],
                vendor_id=drv["vendor"],
                device_id=drv["device"],
                arch=arch,
                category=drv["category"],
                code=code,
                signing_key_path=signing_key,
            )

            vendor_hex = f"{drv['vendor']:04x}"
            device_hex = f"{drv['device']:04x}"

            out_dir = DRIVERS_DIR / arch
            out_dir.mkdir(parents=True, exist_ok=True)
            out_path = out_dir / f"{vendor_hex}_{device_hex}.ajdrv"

            with open(out_path, "wb") as f:
                f.write(package)

            sha = hashlib.sha256(package).hexdigest()

            entry = {
                "name": drv["name"],
                "version": "1.0.0",
                "vendor_id": f"0x{vendor_hex}",
                "device_id": f"0x{device_hex}",
                "arch": arch,
                "category": drv["category"],
                "size_bytes": len(package),
                "sha256": sha,
                "description": drv["desc"],
                "download_url": f"/v1/drivers/{vendor_hex}/{device_hex}/{arch}",
            }
            catalog_entries.append(entry)

            print(f"  {drv['name']:15s} {arch:8s} -> {out_path} ({len(package)} bytes)")

    catalog = {"drivers": catalog_entries, "total": len(catalog_entries)}
    with open(CATALOG_FILE, "w") as f:
        json.dump(catalog, f, indent=2)
        f.write("\n")

    print(f"\nCatalog written: {CATALOG_FILE} ({len(catalog_entries)} entries)")


if __name__ == "__main__":
    main()
