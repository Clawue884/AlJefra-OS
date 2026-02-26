# Contributing to AlJefra OS

AlJefra OS welcomes contributions from developers worldwide. Whether you are
fixing a bug, writing a driver, improving documentation, or adding GUI widgets,
your work helps build the first AI-native operating system.

This guide explains how to contribute effectively.

---

## Table of Contents

1. [Getting Started](#getting-started)
2. [Architecture Overview](#architecture-overview)
3. [Code Style](#code-style)
4. [Building and Testing](#building-and-testing)
5. [Pull Request Process](#pull-request-process)
6. [Code Review Guidelines](#code-review-guidelines)
7. [Commit Message Format](#commit-message-format)
8. [Testing Requirements](#testing-requirements)
9. [Areas Needing Help](#areas-needing-help)
10. [Communication](#communication)
11. [License](#license)

---

## Getting Started

### Prerequisites

Install the following on a Debian/Ubuntu system (or equivalent on your distro):

```bash
sudo apt install nasm gcc make qemu-system-x86 qemu-system-arm qemu-system-misc \
  aarch64-linux-gnu-gcc riscv64-linux-gnu-gcc python3 python3-pip git
```

### Fork, Clone, Build

```bash
# 1. Fork the repository on GitHub

# 2. Clone your fork
git clone https://github.com/<your-user>/AlJefra-OS.git
cd AlJefra-OS

# 3. Build for x86-64
make ARCH=x86_64

# 4. Run on QEMU
qemu-system-x86_64 -machine q35 -cpu Westmere -m 256 -smp 1 \
  -kernel build/x86_64/bin/kernel_x86_64.bin -nographic
```

If the kernel boots to the "AlJefra OS Ready" prompt, your environment is set
up correctly.

---

## Architecture Overview

```
+-------------------------------------------+
|        Applications / AI Agent            |
+-------------------------------------------+
|       Kernel Core (C, portable)           |
|  scheduler - syscalls - driver loader     |
+-------------------------------------------+
|   HAL -- Hardware Abstraction Layer       |
+-------------+-------------+--------------+
|   x86-64    |   AArch64   |  RISC-V 64   |
|   boot.S    |   boot.S    |  boot.S      |
|   APIC/HPET |   GIC/Timer |  PLIC/SBI    |
+-------------+-------------+--------------+
```

**Key directories:**

| Directory           | Purpose                                     |
|---------------------|---------------------------------------------|
| `hal/`              | Hardware abstraction headers (9 headers)     |
| `arch/x86_64/`     | x86-64 architecture implementation           |
| `arch/aarch64/`    | ARM64 architecture implementation            |
| `arch/riscv64/`    | RISC-V 64-bit architecture implementation    |
| `kernel/`          | Portable kernel core (scheduler, loader)     |
| `drivers/`         | Portable C drivers (22+ drivers)             |
| `drivers/runtime/` | Runtime .ajdrv driver build tools            |
| `net/`             | Network stack (TCP/IP, DHCP)                 |
| `store/`           | Marketplace client, Ed25519 verification     |
| `ai/`              | AI bootstrap and evolution framework         |
| `server/`          | Marketplace Flask REST API                   |
| `src/aljefra/`     | x86-64 ASM kernel (legacy kernel path)       |
| `programs/netstack/`| TLS + HTTP + AI agent (BearSSL)             |
| `website/`         | Static website for os.aljefra.com            |
| `doc/`             | Architecture and specification documents     |

---

## Code Style

### C Code

- **Style:** K&R (Kernighan and Ritchie)
- **Indent:** 4 spaces (no tabs)
- **Line length:** 80 characters maximum
- **Braces:** Opening brace on the same line as the statement
- **Naming:** `snake_case` for functions and variables, `UPPER_CASE` for macros
- **Headers:** Include guards using `#ifndef ALJEFRA_<MODULE>_H`
- **Comments:** Use `/* */` for block comments, `//` for single-line comments
- **License header:** Every new file must start with `/* SPDX-License-Identifier: MIT */`

Example:

```c
/* SPDX-License-Identifier: MIT */
/* AlJefra OS -- My driver */

#include "../hal/hal.h"

static uint32_t my_driver_read_reg(volatile void *base, uint32_t offset)
{
    return hal_mmio_read32((volatile uint8_t *)base + offset);
}

hal_status_t my_driver_init(hal_device_t *dev)
{
    volatile void *bar = hal_bus_map_bar(dev, 0);
    if (!bar) {
        hal_console_puts("my_driver: failed to map BAR0\n");
        return HAL_ERR_IO;
    }

    uint32_t status = my_driver_read_reg(bar, REG_STATUS);
    if (status & STATUS_ERROR) {
        return HAL_ERR_DEVICE;
    }

    return HAL_OK;
}
```

### Assembly Code (NASM/GNU AS)

- **Syntax:** NASM Intel syntax for x86-64 kernel files, GNU AS for arch boot
- **Indent:** Tab characters
- **Labels:** `snake_case`, prefixed with module name (e.g., `e1000_init:`)
- **Comments:** `;` for NASM, `//` or `/* */` for GNU AS
- **Section headers:** Use clear section markers

Example (NASM):

```nasm
; AlJefra OS -- Example driver
; SPDX-License-Identifier: MIT

align 16
my_driver_init:
	push rbx
	mov rdi, [dev_base]		; BAR0 base address
	mov eax, [rdi + REG_CTRL]	; Read control register
	or eax, CTRL_RESET
	mov [rdi + REG_CTRL], eax	; Write reset bit
	pop rbx
	ret
```

---

## Building and Testing

### Build Commands

```bash
# Build for a specific architecture
make ARCH=x86_64
make ARCH=aarch64
make ARCH=riscv64

# Build all architectures
make all-arch

# Clean build artifacts
make clean
```

### QEMU Testing

```bash
# x86-64
qemu-system-x86_64 -machine q35 -cpu Westmere -m 256 -smp 1 \
  -kernel build/x86_64/bin/kernel_x86_64.bin -nographic

# ARM64
qemu-system-aarch64 -M virt -cpu cortex-a72 -m 256 \
  -kernel build/aarch64/bin/kernel_aarch64.bin -nographic

# RISC-V 64
qemu-system-riscv64 -M virt -m 256 \
  -kernel build/riscv64/bin/kernel_riscv64.bin -nographic
```

Every change must boot successfully on at least the architecture you modified.
Cross-architecture changes (HAL, kernel, portable drivers) should be tested on
all three.

---

## Pull Request Process

1. **Fork** the repository on GitHub.
2. **Create a branch** from `dev` (not `main`):
   ```bash
   git checkout dev
   git pull origin dev
   git checkout -b my-feature
   ```
3. **Write your code**, following the style guide above.
4. **Test** on QEMU (see above).
5. **Commit** with a descriptive message (see format below).
6. **Push** to your fork:
   ```bash
   git push origin my-feature
   ```
7. **Open a Pull Request** against `dev` on GitHub.
8. **Respond to review feedback** promptly.

### PR Description Template

```
## Summary
Brief description of what this PR does and why.

## Changes
- List of specific changes

## Testing
- How you tested the changes (QEMU command, architecture, etc.)

## Related Issues
Fixes #123 (if applicable)
```

---

## Code Review Guidelines

- All pull requests require **at least one approval** before merging.
- Reviewers should check for:
  - Correctness: Does the code work as intended?
  - Style: Does it follow the C/ASM style guidelines?
  - Safety: No undefined behavior, no buffer overflows, no uninitialized memory.
  - Portability: Does it work across all three architectures (if applicable)?
  - Testing: Has the author tested on QEMU?
- Be constructive and specific in review comments.
- If a PR is large, consider breaking it into smaller, reviewable pieces.

---

## Commit Message Format

Use the following format:

```
component: short description

Optional longer description explaining the motivation and approach.
```

**Examples:**

```
kernel/fs: add file rename support

The BMFS filesystem lacked a rename operation. This adds fs_rename()
which updates the directory entry in-place without copying data.
```

```
drivers/net: fix e1000 link-up detection on I219-LM
```

```
hal: add hal_timer_deadline() for one-shot timer support
```

```
doc: update hardware compatibility list with NVMe results
```

**Component prefixes:**

| Prefix            | Scope                                |
|-------------------|--------------------------------------|
| `kernel/`         | Kernel core (scheduler, loader)      |
| `kernel/fs`       | Filesystem                           |
| `hal/`            | HAL headers and contracts            |
| `arch/x86_64`     | x86-64 architecture code             |
| `arch/aarch64`    | ARM64 architecture code              |
| `arch/riscv64`    | RISC-V architecture code             |
| `drivers/storage`  | Storage drivers                     |
| `drivers/net`     | Network drivers                      |
| `drivers/input`   | Input drivers                        |
| `drivers/display` | Display drivers                      |
| `store/`          | Marketplace client                   |
| `net/`            | Network stack                        |
| `ai/`             | AI subsystem                         |
| `gui/`            | GUI / desktop                        |
| `doc/`            | Documentation                        |
| `build/`          | Build system / Makefile              |

---

## Testing Requirements

Before submitting a PR:

1. **Boot test:** Your change must not break boot on any architecture you
   touched. Run the QEMU commands listed above.
2. **No regressions:** If you modify a driver, verify that existing
   functionality still works (e.g., a storage driver can still read/write).
3. **New drivers:** Must include at least one QEMU test scenario.
4. **Runtime drivers (.ajdrv):** Must build successfully with
   `drivers/runtime/build_ajdrv.sh` and load via the marketplace flow.
5. **Cross-arch changes:** If you modify code in `hal/`, `kernel/`, or
   portable drivers, test on all three architectures.

---

## Areas Needing Help

We especially welcome contributions in these areas:

- **Drivers:** New hardware drivers (audio, Bluetooth, AMD GPU, Intel GPU)
- **GUI widgets:** Button, textbox, scrollbar, file browser, terminal emulator
- **Desktop shell:** Window manager, taskbar, theme engine
- **Documentation:** Tutorials, API docs, getting-started guides
- **Testing:** Hardware testing on real devices, automated CI/CD
- **Translations:** Arabic, French, Spanish, Chinese, and more
- **AI chat system:** Natural language command processing, offline SLM
- **Security:** Secure boot chain, memory protection, crash recovery
- **Tooling:** CI pipeline, automated QEMU boot tests, code coverage

---

## Communication

- **Bug reports:** Open a GitHub Issue with reproduction steps and QEMU output.
- **Feature requests:** Open a GitHub Discussion under the "Ideas" category.
- **Questions:** Open a GitHub Discussion under the "Q&A" category.
- **Security issues:** Email security@qatarit.com. Do NOT open a public issue.

---

## Code of Conduct

All contributors must follow the [Code of Conduct](CODE_OF_CONDUCT.md). We are
committed to a welcoming, inclusive, harassment-free community.

---

## License

AlJefra OS is released under the **MIT License**. By contributing, you agree
that your contributions will be licensed under the same terms.

See [LICENSE](LICENSE) for the full text.

---

*AlJefra OS -- Built in Qatar. Built for the world.*
*Qatar IT -- www.QatarIT.com*
