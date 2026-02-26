# SPDX-License-Identifier: MIT
# AlJefra OS -- Driver Marketplace Data Models

"""
Data models for the AlJefra Driver Marketplace API.

These mirror the C structs in store/package.h and ai/marketplace.h.
"""

from dataclasses import dataclass, field, asdict
from enum import IntEnum
from typing import List, Optional


# ── Architecture codes (matches store/package.h AJDRV_ARCH_*) ──

class Arch(IntEnum):
    X86_64  = 0
    AARCH64 = 1
    RISCV64 = 2
    ANY     = 0xFF

    @classmethod
    def from_string(cls, s: str) -> "Arch":
        mapping = {
            "x86_64":  cls.X86_64,
            "aarch64": cls.AARCH64,
            "riscv64": cls.RISCV64,
            "any":     cls.ANY,
        }
        return mapping.get(s.lower(), cls.X86_64)

    def to_string(self) -> str:
        mapping = {
            self.X86_64:  "x86_64",
            self.AARCH64: "aarch64",
            self.RISCV64: "riscv64",
            self.ANY:     "any",
        }
        return mapping.get(self, "x86_64")


# ── Driver category codes (matches store/package.h AJDRV_CAT_*) ──

class Category(IntEnum):
    STORAGE = 0
    NETWORK = 1
    INPUT   = 2
    DISPLAY = 3
    GPU     = 4
    BUS     = 5
    OTHER   = 6

    @classmethod
    def from_string(cls, s: str) -> "Category":
        mapping = {
            "storage": cls.STORAGE,
            "network": cls.NETWORK,
            "input":   cls.INPUT,
            "display": cls.DISPLAY,
            "gpu":     cls.GPU,
            "bus":     cls.BUS,
            "other":   cls.OTHER,
        }
        return mapping.get(s.lower(), cls.OTHER)

    def to_string(self) -> str:
        mapping = {
            self.STORAGE: "storage",
            self.NETWORK: "network",
            self.INPUT:   "input",
            self.DISPLAY: "display",
            self.GPU:     "gpu",
            self.BUS:     "bus",
            self.OTHER:   "other",
        }
        return mapping.get(self, "other")


# ── Driver metadata ──

@dataclass
class DriverMeta:
    """Metadata for a driver in the catalog.
    Corresponds to catalog_entry_t in store/catalog.h and the JSON
    returned by GET /v1/catalog."""

    name: str                      # e.g. "virtio_blk"
    version: str                   # e.g. "1.0.0"
    vendor_id: str                 # 4-hex-char, e.g. "1af4"
    device_id: str                 # 4-hex-char, e.g. "1001"
    arch: str                      # "x86_64", "aarch64", "riscv64"
    category: str                  # "storage", "network", etc.
    size_bytes: int = 0            # set after .ajdrv is built
    sha256: str = ""               # hex SHA-256 of .ajdrv file
    description: str = ""
    min_os_version: str = "1.0"
    flags: int = 0                 # AJDRV_FLAG_* bitmask
    download_url: str = ""         # auto-generated

    def fill_download_url(self) -> None:
        self.download_url = (
            f"/v1/drivers/{self.vendor_id}/{self.device_id}/{self.arch}"
        )

    def to_catalog_json(self) -> dict:
        """JSON representation matching the marketplace_spec.md catalog format."""
        self.fill_download_url()
        return {
            "name": self.name,
            "version": self.version,
            "vendor_id": f"0x{self.vendor_id}",
            "device_id": f"0x{self.device_id}",
            "arch": self.arch,
            "category": self.category,
            "size_bytes": self.size_bytes,
            "sha256": self.sha256,
            "description": self.description,
            "download_url": self.download_url,
        }


# ── Hardware manifest (matches marketplace.c JSON output) ──

@dataclass
class ManifestDevice:
    """A single device entry from the client's POST /v1/manifest body.
    Fields match the short keys used in marketplace.c:
      v = vendor_id, d = device_id, c = class_code, s = subclass
    Also accepts the long-form keys used in the spec examples."""

    vendor: str     # hex string, e.g. "8086"
    device: str     # hex string, e.g. "10d3"
    class_code: int = 0
    subclass: int = 0
    has_driver: bool = False

    @classmethod
    def from_json(cls, obj: dict) -> "ManifestDevice":
        return cls(
            vendor=obj.get("v", obj.get("vendor", "")),
            device=obj.get("d", obj.get("device", "")),
            class_code=int(obj.get("c", obj.get("class", 0))),
            subclass=int(obj.get("s", obj.get("subclass", 0))),
            has_driver=bool(obj.get("has_drv", obj.get("has_driver", False))),
        )


@dataclass
class HardwareManifest:
    """The full hardware manifest sent by the OS client."""

    arch: str = "x86_64"
    cpu_vendor: str = ""
    cpu_model: str = ""
    ram_mb: int = 0
    devices: List[ManifestDevice] = field(default_factory=list)

    @classmethod
    def from_json(cls, obj: dict) -> "HardwareManifest":
        devices = [ManifestDevice.from_json(d) for d in obj.get("devices", [])]
        return cls(
            arch=obj.get("arch", "x86_64"),
            cpu_vendor=obj.get("cpu_vendor", ""),
            cpu_model=obj.get("cpu_model", ""),
            ram_mb=int(obj.get("ram_mb", 0)),
            devices=devices,
        )


# ── Driver recommendation (response to POST /v1/manifest) ──

@dataclass
class DriverRecommendation:
    vendor_id: str
    device_id: str
    driver_name: str
    version: str
    priority: str       # "critical", "recommended", "optional"
    download_url: str

    def to_json(self) -> dict:
        return {
            "vendor_id": self.vendor_id,
            "device_id": self.device_id,
            "driver_name": self.driver_name,
            "version": self.version,
            "priority": self.priority,
            "download_url": self.download_url,
        }
