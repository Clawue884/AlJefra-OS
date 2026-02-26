# AlJefra OS — Memory Maps

## x86-64 Memory Map

### Physical Memory Layout
```
0x0000_0000 - 0x0000_0FFF   4 KB    Real-mode IVT (unused in long mode)
0x0000_1000 - 0x0000_4FFF  16 KB    Pure64 page tables (PML4)
0x0000_5000 - 0x0000_5FFF   4 KB    Pure64 info table
  0x5010: Total memory (bytes, uint64)
  0x5020: Memory map entry count
  0x5040: BSP APIC ID
  0x5060: Number of activated APs
0x0000_6000 - 0x0000_6FFF   4 KB    E820 memory map entries
0x0000_8000 - 0x0000_FFFF  32 KB    Pure64 bootloader
0x0010_0000 - 0x0010_FFFF  64 KB    AlJefra kernel
  0x100010: API dispatch table (dq pointers)
  0x100010: b_input
  0x100018: b_output
  0x100020: b_net_tx
  0x100028: b_net_rx
  0x100030: b_nvs_read
  0x100038: b_nvs_write
  0x100040: b_system
  0x100048: (reserved)
  0x100050: b_gpu_status
  ...
  0x1000A0: b_gpu_benchmark
0x0011_0000 - 0x0011_FFFF  64 KB    Kernel system variables
0x0011_A000                          net_table (NIC info, MAC at +0x08)
0x0012_0000 - 0x0012_1FFF   8 KB    AI scheduler queue
0x001E_0000 - 0x002D_FFFF   1 MB    Payload load area (C kernel + drivers)
0x0040_0000 - 0x0040_FFFF  64 KB    GPU command queue
0x0080_0000 - 0x00FF_FFFF   8 MB    Task stacks (64 tasks × 128 KB)
0x0100_0000 - 0x01FF_FFFF  16 MB    DMA buffer pool
0x0200_0000 - 0x03FF_FFFF  32 MB    Page table / bitmap allocator
```

### Virtual Memory
Identity-mapped by Pure64 (virtual == physical) for first 4 GB using 2MB pages. The C kernel extends mappings as needed for MMIO regions.

## AArch64 Memory Map (QEMU virt machine)

### Physical Memory Layout
```
0x0000_0000 - 0x03FF_FFFF  64 MB    Flash (firmware)
0x0800_0000 - 0x0800_FFFF  64 KB    GICv2 Distributor (GICD)
0x0801_0000 - 0x0801_FFFF  64 KB    GICv2 CPU Interface (GICC)
0x0802_0000 - 0x0802_FFFF  64 KB    GICv2 Virtual Interface (GICV)
0x0803_0000 - 0x0803_FFFF  64 KB    GICv2 Hypervisor (GICH)
0x0900_0000 - 0x0900_0FFF   4 KB    PL011 UART
0x0901_0000 - 0x0901_0FFF   4 KB    RTC (PL031)
0x0A00_0000 - 0x0A00_3FFF  16 KB    VirtIO MMIO device 0
0x0A00_4000 - ...                    VirtIO MMIO devices 1-31
0x0C00_0000 - 0x0DFF_FFFF  32 MB    Platform bus
0x1000_0000 - 0x1FFF_FFFF 256 MB    PCIe ECAM
0x2000_0000 - 0x2FFF_FFFF 256 MB    PCIe MMIO (32-bit)
0x4000_0000 - ...                    RAM (kernel loaded here)

Kernel layout at 0x4000_0000:
  .text                              Code
  .rodata                            Read-only data
  .data                              Initialized data
  .bss                               Zero-initialized data
  + 64 KB gap                        Stack (grows down)
  DMA pool                           16 MB for DMA buffers
  Page tables                        MMU structures
```

### Virtual Memory
Identity-mapped for RAM and device MMIO regions. Uses TTBR0_EL1 with 4KB granule, 48-bit VA.

## RISC-V 64 Memory Map (QEMU virt machine)

### Physical Memory Layout
```
0x0000_1000 - 0x0000_1FFF   4 KB    Boot ROM
0x0200_0000 - 0x0200_FFFF  64 KB    CLINT (Core Local Interruptor)
  0x0200_0000: msip[hart]             Machine software interrupt pending
  0x0200_4000: mtimecmp[hart]         Timer compare
  0x0200_BFF8: mtime                  Timer value
0x0C00_0000 - 0x0FFF_FFFF  64 MB    PLIC (Platform Level Interrupt Controller)
  0x0C00_0004: Priority[1..1023]
  0x0C00_2000: Pending bits
  0x0C20_0000: Enable bits[context]
  0x0C20_0000: Priority threshold[context]
  0x0C20_0004: Claim/complete[context]
0x1000_0000 - 0x1000_0FFF   4 KB    UART (16550-compatible)
0x1000_1000 - ...                    VirtIO MMIO devices
0x3000_0000 - 0x3FFF_FFFF 256 MB    PCIe ECAM
0x4000_0000 - 0x7FFF_FFFF   1 GB    PCIe MMIO
0x8000_0000 - ...                    RAM (kernel loaded here)

Kernel layout at 0x8000_0000:
  .text                              Code
  .rodata                            Read-only data
  .data                              Initialized data
  .bss                               Zero-initialized data
  + 64 KB gap                        Stack
  DMA pool                           16 MB
  Page tables                        Sv48 structures
```

### Virtual Memory
Identity-mapped using Sv48 (4-level page tables, 48-bit virtual address, 4KB pages). satp CSR points to root page table.

## DMA Buffer Requirements

All architectures use identity mapping (virt == phys), so DMA buffers can use physical addresses directly. Key requirements:

| Requirement | x86-64 | AArch64 | RISC-V |
|------------|--------|---------|--------|
| Alignment | 4 KB | 4 KB | 4 KB |
| Cache coherent | Yes (x86 is snooping) | No (need cache flush/inv) | No (need fence) |
| Below 4 GB | Required for legacy PCI | No restriction | No restriction |
| Contiguous | Required for DMA | Required | Required |
