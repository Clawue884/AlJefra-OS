# SPDX-License-Identifier: MIT
# AlJefra OS -- Driver Storage Backend

"""
File-based driver storage.

Layout:
    server/drivers/{arch}/{vendor_id}_{device_id}.ajdrv
    server/catalog.json

The catalog.json file is the source of truth for all metadata.
Driver binaries live under drivers/ keyed by (arch, vendor_id, device_id).
"""

import hashlib
import json
import os
from pathlib import Path
from typing import Dict, List, Optional, Tuple

from models import Arch, Category, DriverMeta


# ── Paths ──

BASE_DIR = Path(__file__).resolve().parent
DRIVERS_DIR = BASE_DIR / "drivers"
CATALOG_FILE = BASE_DIR / "catalog.json"


class DriverStore:
    """In-memory catalog backed by catalog.json + on-disk .ajdrv files."""

    def __init__(self) -> None:
        # Indexed by (vendor_id, device_id, arch) -- all lowercase hex strings
        self._catalog: Dict[Tuple[str, str, str], DriverMeta] = {}
        self._load_catalog()

    # ── Catalog persistence ──

    def _load_catalog(self) -> None:
        """Load catalog.json into memory."""
        if not CATALOG_FILE.exists():
            return
        with open(CATALOG_FILE, "r") as f:
            data = json.load(f)
        for entry in data.get("drivers", []):
            meta = DriverMeta(
                name=entry["name"],
                version=entry["version"],
                vendor_id=entry["vendor_id"].replace("0x", "").lower(),
                device_id=entry["device_id"].replace("0x", "").lower(),
                arch=entry["arch"],
                category=entry["category"],
                size_bytes=entry.get("size_bytes", 0),
                sha256=entry.get("sha256", ""),
                description=entry.get("description", ""),
                min_os_version=entry.get("min_os_version", "1.0"),
                flags=entry.get("flags", 0),
            )
            key = (meta.vendor_id, meta.device_id, meta.arch)
            self._catalog[key] = meta

    def _save_catalog(self) -> None:
        """Persist catalog to disk."""
        drivers = []
        for meta in sorted(self._catalog.values(), key=lambda m: m.name):
            drivers.append(meta.to_catalog_json())
        data = {"drivers": drivers, "total": len(drivers)}
        with open(CATALOG_FILE, "w") as f:
            json.dump(data, f, indent=2)
            f.write("\n")

    # ── Query ──

    def list_all(
        self,
        arch: Optional[str] = None,
        category: Optional[str] = None,
        page: int = 1,
        per_page: int = 50,
    ) -> Tuple[List[dict], int]:
        """Return catalog entries, optionally filtered. Returns (items, total)."""
        entries = list(self._catalog.values())

        if arch:
            entries = [e for e in entries if e.arch == arch]
        if category:
            entries = [e for e in entries if e.category == category]

        total = len(entries)

        # Sort by name then arch for deterministic output
        entries.sort(key=lambda e: (e.name, e.arch))

        # Pagination
        start = (page - 1) * per_page
        end = start + per_page
        page_entries = entries[start:end]

        return [e.to_catalog_json() for e in page_entries], total

    def find(
        self, vendor_id: str, device_id: str, arch: str
    ) -> Optional[DriverMeta]:
        """Find a specific driver by (vendor, device, arch)."""
        key = (vendor_id.lower(), device_id.lower(), arch)
        return self._catalog.get(key)

    def find_for_device(
        self, vendor_id: str, device_id: str, arch: str
    ) -> Optional[DriverMeta]:
        """Find a driver matching a device, checking arch-specific first,
        then fallback to 'any' arch."""
        meta = self.find(vendor_id, device_id, arch)
        if meta:
            return meta
        # Try architecture-independent driver
        key = (vendor_id.lower(), device_id.lower(), "any")
        return self._catalog.get(key)

    # ── Driver binary access ──

    def driver_path(self, vendor_id: str, device_id: str, arch: str) -> Path:
        """Filesystem path for a driver binary."""
        v = vendor_id.lower()
        d = device_id.lower()
        return DRIVERS_DIR / arch / f"{v}_{d}.ajdrv"

    def get_driver_binary(
        self, vendor_id: str, device_id: str, arch: str
    ) -> Optional[bytes]:
        """Read a driver binary from disk. Returns None if not found."""
        path = self.driver_path(vendor_id, device_id, arch)
        if not path.exists():
            return None
        with open(path, "rb") as f:
            return f.read()

    # ── Add / Upload ──

    def add_driver(self, meta: DriverMeta, binary: bytes) -> None:
        """Store a new driver binary and update the catalog."""
        v = meta.vendor_id.lower()
        d = meta.device_id.lower()
        arch = meta.arch

        # Ensure directory exists
        arch_dir = DRIVERS_DIR / arch
        arch_dir.mkdir(parents=True, exist_ok=True)

        # Write binary
        path = arch_dir / f"{v}_{d}.ajdrv"
        with open(path, "wb") as f:
            f.write(binary)

        # Update metadata
        meta.size_bytes = len(binary)
        meta.sha256 = hashlib.sha256(binary).hexdigest()
        meta.fill_download_url()

        key = (v, d, arch)
        self._catalog[key] = meta

        self._save_catalog()

    def remove_driver(
        self, vendor_id: str, device_id: str, arch: str
    ) -> bool:
        """Remove a driver from the store. Returns True if it existed."""
        key = (vendor_id.lower(), device_id.lower(), arch)
        if key not in self._catalog:
            return False

        # Remove binary
        path = self.driver_path(vendor_id, device_id, arch)
        if path.exists():
            path.unlink()

        del self._catalog[key]
        self._save_catalog()
        return True

    # ── Recommendation engine ──

    def recommend(
        self, vendor_id: str, device_id: str, arch: str, has_driver: bool
    ) -> Optional[dict]:
        """Generate a driver recommendation for a device.
        Returns a recommendation dict or None."""
        meta = self.find_for_device(vendor_id, device_id, arch)
        if meta is None:
            return None

        # Determine priority
        if has_driver:
            priority = "optional"  # already has a built-in driver
        else:
            # Classify by category
            cat = Category.from_string(meta.category)
            if cat in (Category.STORAGE, Category.NETWORK):
                priority = "critical"
            elif cat in (Category.DISPLAY, Category.GPU):
                priority = "recommended"
            else:
                priority = "optional"

        meta.fill_download_url()
        return {
            "vendor_id": meta.vendor_id,
            "device_id": meta.device_id,
            "driver_name": meta.name,
            "version": meta.version,
            "priority": priority,
            "download_url": meta.download_url,
        }
