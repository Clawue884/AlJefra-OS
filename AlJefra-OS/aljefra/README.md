# AlJefra Kernel

The x86-64 assembly exokernel core of AlJefra OS.

## Overview

This directory contains the original x86-64 assembly kernel — the foundation of AlJefra OS. It provides:

- **Mono-processing, multi-core**: Executes a single program spread across available CPU cores
- **BIOS/UEFI boot**: Via the Pure64 loader
- **Minimal footprint**: Kernel binary < 32 KiB, uses only 4 MiB RAM
- **Exokernel design**: Direct hardware access with zero overhead

## Structure

```
aljefra/
├── src/           # Kernel assembly source
│   ├── kernel.asm     # Main kernel entry
│   ├── init/          # Initialization routines
│   ├── syscalls/      # System call handlers
│   ├── drivers/       # Hardware drivers (asm)
│   └── sysvar.asm     # System variables
├── api/           # User-space API
│   ├── libAlJefra.h   # C API header
│   ├── libAlJefra.c   # C API implementation
│   └── libAlJefra.asm # Assembly API
├── bin/           # Build output
├── doc/           # Kernel documentation
└── build.sh       # Build script
```

## Building

```bash
cd aljefra && ./build.sh
```

Or use the top-level build system:
```bash
make ARCH=x86_64
```

## License

MIT License. See [LICENSE](LICENSE).
