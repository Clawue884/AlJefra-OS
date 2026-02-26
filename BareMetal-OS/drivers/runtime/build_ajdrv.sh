#!/bin/bash
# Build a .ajdrv driver package from a C source file.
# Usage: ./build_ajdrv.sh <source.c> <arch> <vendor_id> <device_id> <category> <name> <desc>
#
# Example: ./build_ajdrv.sh qemu_vga.c x86_64 1234 1111 3 qemu_vga "QEMU Standard VGA"

set -e

SRC="$1"
ARCH="$2"
VENDOR_ID="$3"
DEVICE_ID="$4"
CATEGORY="$5"
NAME="$6"
DESC="$7"

if [ -z "$SRC" ] || [ -z "$ARCH" ]; then
    echo "Usage: $0 <source.c> <arch> <vendor_id> <device_id> <category> <name> <desc>"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BASE="$(basename "$SRC" .c)"

# Architecture-specific settings
case "$ARCH" in
    x86_64)
        CC=gcc
        CFLAGS="-m64 -march=x86-64 -mno-red-zone -fPIC"
        ARCH_CODE=0
        ;;
    aarch64)
        CC=aarch64-linux-gnu-gcc
        CFLAGS="-march=armv8-a"
        ARCH_CODE=1
        ;;
    riscv64)
        CC=riscv64-linux-gnu-gcc
        CFLAGS="-march=rv64gc -mabi=lp64d"
        ARCH_CODE=2
        ;;
    *)
        echo "Unknown arch: $ARCH"
        exit 1
        ;;
esac

OBJCOPY="${CC/gcc/objcopy}"
[ "$CC" = "gcc" ] && OBJCOPY="objcopy"

TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

# Step 1: Compile as position-independent code
$CC $CFLAGS -ffreestanding -fno-stack-protector -fPIC -nostdinc -nostdlib \
    -Wall -O2 -fno-tree-vectorize -I"$SCRIPT_DIR/../.." \
    -isystem /usr/lib/gcc/x86_64-linux-gnu/14/include \
    -c "$SRC" -o "$TMPDIR/$BASE.o"

# Step 2: Link as flat binary with entry at offset 0
# Put driver_entry first by specifying it in the linker script
ld -T "$SCRIPT_DIR/flat.ld" -nostdlib -o "$TMPDIR/$BASE.elf" "$TMPDIR/$BASE.o"

# Step 3: Extract flat binary
$OBJCOPY -O binary "$TMPDIR/$BASE.elf" "$TMPDIR/code.bin"

CODE_SIZE=$(stat -c%s "$TMPDIR/code.bin")

# Step 4: Find the entry point offset (driver_entry symbol)
ENTRY_OFFSET=$(nm "$TMPDIR/$BASE.elf" | grep ' driver_entry$' | awk '{print $1}')
if [ -z "$ENTRY_OFFSET" ]; then
    echo "ERROR: driver_entry symbol not found"
    exit 1
fi
ENTRY_OFFSET=$((16#$ENTRY_OFFSET))

echo "Driver: $NAME ($DESC)"
echo "  Arch: $ARCH (code $ARCH_CODE)"
echo "  Code: $CODE_SIZE bytes, entry at offset $ENTRY_OFFSET"

# Step 5: Build .ajdrv package
#
# Header: 64 bytes
# Name: at offset 64
# Desc: after name
# Code: after desc
# Signature: after code (64 bytes placeholder)

NAME_SIZE=$((${#NAME} + 1))
DESC_SIZE=$((${#DESC} + 1))

NAME_OFFSET=64
DESC_OFFSET=$((NAME_OFFSET + NAME_SIZE))
CODE_OFFSET=$((DESC_OFFSET + DESC_SIZE))
# Align code to 16 bytes
CODE_OFFSET=$(( (CODE_OFFSET + 15) & ~15 ))
SIG_OFFSET=$((CODE_OFFSET + CODE_SIZE))

TOTAL_SIZE=$((SIG_OFFSET + 64))

echo "  Package: $TOTAL_SIZE bytes (header=64, name=$NAME_SIZE, desc=$DESC_SIZE, code=$CODE_SIZE, sig=64)"

# Build the package using python
python3 -c "
import struct, sys

# Header fields
magic = 0x56444A41  # 'AJDV'
version = 1
arch = $ARCH_CODE
category = $CATEGORY
code_offset = $CODE_OFFSET
code_size = $CODE_SIZE
name_offset = $NAME_OFFSET
name_size = $NAME_SIZE
desc_offset = $DESC_OFFSET
desc_size = $DESC_SIZE
entry_offset = $ENTRY_OFFSET
sig_offset = $SIG_OFFSET
vendor_id = int('$VENDOR_ID', 16)
device_id = int('$DEVICE_ID', 16)
min_os_ver = 0x0100  # 1.0
flags = 0

# Pack header (64 bytes)
hdr = struct.pack('<IIIIIIIIIIIIHHHH8s',
    magic, version, arch, category,
    code_offset, code_size,
    name_offset, name_size,
    desc_offset, desc_size,
    entry_offset, sig_offset,
    vendor_id, device_id, min_os_ver, flags,
    b'\x00' * 8)

assert len(hdr) == 64

# Build package
pkg = bytearray(sig_offset + 64)
pkg[0:64] = hdr
pkg[name_offset:name_offset+name_size] = b'$NAME\x00'
pkg[desc_offset:desc_offset+desc_size] = b'$DESC\x00'

# Read code
with open('$TMPDIR/code.bin', 'rb') as f:
    code = f.read()
pkg[code_offset:code_offset+len(code)] = code

# Signature placeholder (all zeros)
pkg[sig_offset:sig_offset+64] = b'\x00' * 64

# Write package
outpath = '$SCRIPT_DIR/${BASE}_${ARCH}.ajdrv'
with open(outpath, 'wb') as f:
    f.write(pkg)
print(f'  Output: {outpath}')
"
