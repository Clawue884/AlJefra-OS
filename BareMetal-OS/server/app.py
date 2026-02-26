#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# AlJefra OS -- Driver Marketplace REST API Server
#
# Endpoints:
#   GET  /v1/catalog                          -- list all drivers
#   POST /v1/manifest                         -- send hw manifest, get recommendations
#   GET  /v1/drivers/<vendor>/<device>/<arch>  -- download .ajdrv binary
#   POST /v1/drivers                          -- upload new driver package
#   GET  /v1/updates/<os_version>             -- check for OS/driver updates
#
# Quick start:
#   pip install -r requirements.txt
#   python app.py                # serves on 0.0.0.0:8080
#
# The server auto-seeds stub drivers on first run if catalog.json is absent.

import hashlib
import json
import os
import struct
import sys
from pathlib import Path

from flask import Flask, Response, jsonify, request, send_file
from flask_cors import CORS

# Add server/ to path so models and driver_store can be imported
sys.path.insert(0, str(Path(__file__).resolve().parent))

from models import Arch, Category, DriverMeta, HardwareManifest
from driver_store import DriverStore, CATALOG_FILE, DRIVERS_DIR

# ── Application setup ──

app = Flask(__name__)
CORS(app)  # Allow web dashboard access

# Current OS version advertised by the server
OS_VERSION = "1.0.0"

# Initialize the driver store (loads catalog.json)
store = DriverStore()


# ── Auto-seed on first run ──

def _auto_seed() -> None:
    """If no catalog.json exists, run the seed builder."""
    if CATALOG_FILE.exists():
        return
    print("[seed] No catalog.json found -- generating stub drivers...")
    from ajdrv_builder import seed_catalog
    seed_catalog()
    # Reload the store after seeding
    global store
    store = DriverStore()
    print("[seed] Done.\n")


# ── Endpoints ──

@app.route("/", methods=["GET"])
def index():
    """Health check / API info."""
    return jsonify({
        "service": "AlJefra Driver Marketplace",
        "version": "1.0.0",
        "endpoints": {
            "catalog":  "GET  /v1/catalog",
            "manifest": "POST /v1/manifest",
            "driver":   "GET  /v1/drivers/<vendor>/<device>/<arch>",
            "upload":   "POST /v1/drivers",
            "updates":  "GET  /v1/updates/<os_version>",
            "evolve":   "POST /v1/evolve",
            "reviews":  "GET/POST /v1/reviews",
            "metrics":  "POST /v1/metrics",
        },
    })


@app.route("/v1/catalog", methods=["GET"])
def get_catalog():
    """List all available drivers.

    Query params:
        arch     -- filter by architecture (x86_64, aarch64, riscv64)
        category -- filter by category (storage, network, input, display, gpu, bus)
        page     -- page number (default 1)
        per_page -- items per page (default 50)

    Response:
        { "drivers": [...], "total": N, "page": P }
    """
    arch = request.args.get("arch")
    category = request.args.get("category")
    page = int(request.args.get("page", 1))
    per_page = int(request.args.get("per_page", 50))

    drivers, total = store.list_all(
        arch=arch, category=category, page=page, per_page=per_page
    )

    return jsonify({
        "drivers": drivers,
        "total": total,
        "page": page,
    })


@app.route("/v1/manifest", methods=["POST"])
def post_manifest():
    """Receive a hardware manifest, return driver recommendations.

    The client (marketplace.c) sends JSON like:
        {
            "arch": "x86_64",
            "cpu_vendor": "GenuineIntel",
            "cpu_model": "...",
            "ram_mb": 256,
            "devices": [
                {"v": "8086", "d": "10d3", "c": 2, "s": 0, "has_drv": false},
                ...
            ]
        }

    Response:
        {
            "recommendations": [
                {
                    "vendor_id": "8086",
                    "device_id": "10d3",
                    "driver_name": "e1000",
                    "version": "1.0.0",
                    "priority": "critical",
                    "download_url": "/v1/drivers/8086/10d3/x86_64"
                }
            ],
            "os_update_available": false
        }
    """
    body = request.get_json(silent=True)
    if body is None:
        return jsonify({"error": "Invalid JSON body"}), 400

    manifest = HardwareManifest.from_json(body)
    recommendations = []

    for dev in manifest.devices:
        rec = store.recommend(
            vendor_id=dev.vendor,
            device_id=dev.device,
            arch=manifest.arch,
            has_driver=dev.has_driver,
        )
        if rec is not None:
            recommendations.append(rec)

    return jsonify({
        "recommendations": recommendations,
        "os_update_available": False,
    })


@app.route("/v1/drivers/<vendor>/<device>/<arch>", methods=["GET"])
def get_driver(vendor: str, device: str, arch: str):
    """Download a .ajdrv driver package.

    Path params:
        vendor -- 4-hex-char vendor ID (e.g. "8086")
        device -- 4-hex-char device ID (e.g. "10d3")
        arch   -- target architecture (x86_64, aarch64, riscv64)

    Response:
        Binary .ajdrv file with Content-Type: application/octet-stream
    """
    vendor = vendor.lower()
    device = device.lower()
    arch = arch.lower()

    # Validate arch
    if arch not in ("x86_64", "aarch64", "riscv64"):
        return jsonify({"error": f"Unknown architecture: {arch}"}), 400

    # Check if driver exists in catalog
    meta = store.find(vendor, device, arch)
    if meta is None:
        return jsonify({
            "error": "Driver not found",
            "vendor_id": vendor,
            "device_id": device,
            "arch": arch,
        }), 404

    # Read binary
    binary = store.get_driver_binary(vendor, device, arch)
    if binary is None:
        return jsonify({
            "error": "Driver binary missing from storage",
            "vendor_id": vendor,
            "device_id": device,
            "arch": arch,
        }), 500

    filename = f"{vendor}_{device}_{arch}.ajdrv"
    return Response(
        binary,
        mimetype="application/octet-stream",
        headers={
            "Content-Disposition": f'attachment; filename="{filename}"',
            "Content-Length": str(len(binary)),
            "X-AJDRV-Name": meta.name,
            "X-AJDRV-Version": meta.version,
            "X-AJDRV-SHA256": meta.sha256,
        },
    )


@app.route("/v1/drivers", methods=["POST"])
def upload_driver():
    """Upload a new .ajdrv driver package.

    Accepts multipart/form-data with:
        file     -- the .ajdrv binary
        metadata -- JSON string with optional extra fields

    Or application/octet-stream with the raw .ajdrv bytes
    (metadata extracted from the package header).
    """
    # Check for multipart upload
    if "file" in request.files:
        f = request.files["file"]
        binary = f.read()
        # Optional metadata JSON
        meta_json = request.form.get("metadata", "{}")
        try:
            extra = json.loads(meta_json)
        except json.JSONDecodeError:
            extra = {}
    elif request.content_type and "octet-stream" in request.content_type:
        binary = request.data
        extra = {}
    else:
        return jsonify({"error": "Upload .ajdrv via multipart 'file' field or application/octet-stream"}), 400

    if len(binary) < 64 + 64:
        return jsonify({"error": "File too small to be a valid .ajdrv package"}), 400

    # Parse the .ajdrv header
    AJDRV_MAGIC = 0x56444A41
    magic = struct.unpack_from("<I", binary, 0)[0]
    if magic != AJDRV_MAGIC:
        return jsonify({"error": f"Bad magic: 0x{magic:08X}, expected 0x{AJDRV_MAGIC:08X}"}), 400

    (
        _magic, _version, arch_code, cat_code,
        code_offset, code_size,
        name_offset, name_size,
        desc_offset, desc_size,
        _entry_offset, _sig_offset,
        vendor_id, device_id,
        _min_os, flags,
    ) = struct.unpack_from("<IIIIIIIIIIIIHHHH", binary, 0)

    # Extract name and description
    name = binary[name_offset:name_offset + name_size].rstrip(b"\x00").decode("utf-8", errors="replace")
    desc = binary[desc_offset:desc_offset + desc_size].rstrip(b"\x00").decode("utf-8", errors="replace")

    # Map codes to strings
    arch_str = {0: "x86_64", 1: "aarch64", 2: "riscv64", 0xFF: "any"}.get(arch_code, "x86_64")
    cat_str = {0: "storage", 1: "network", 2: "input", 3: "display", 4: "gpu", 5: "bus", 6: "other"}.get(cat_code, "other")

    vendor_hex = f"{vendor_id:04x}"
    device_hex = f"{device_id:04x}"

    meta = DriverMeta(
        name=name,
        version=extra.get("version", "1.0.0"),
        vendor_id=vendor_hex,
        device_id=device_hex,
        arch=arch_str,
        category=cat_str,
        description=desc,
        flags=flags,
    )

    store.add_driver(meta, binary)

    return jsonify({
        "status": "ok",
        "driver": meta.to_catalog_json(),
    }), 201


@app.route("/v1/updates/<os_version>", methods=["GET"])
def check_updates(os_version: str):
    """Check for OS/driver updates.

    Response:
        {
            "update_available": true/false,
            "version": "2.0.0",
            "changelog": "...",
            "download_url": "/v1/updates/2.0.0/x86_64"
        }
    """
    # Simple version comparison: if the client version < server OS_VERSION
    client_parts = os_version.split(".")
    server_parts = OS_VERSION.split(".")

    update_available = False
    try:
        for c, s in zip(client_parts, server_parts):
            if int(s) > int(c):
                update_available = True
                break
            elif int(s) < int(c):
                break
    except ValueError:
        pass

    return jsonify({
        "update_available": update_available,
        "version": OS_VERSION,
        "changelog": "Multi-architecture support, HAL layer, portable drivers" if update_available else "",
        "download_url": f"/v1/updates/{OS_VERSION}/x86_64" if update_available else "",
    })


# ── AI Evolution ──

# In-memory evolution log (production would use a database)
_evolution_log = []

@app.route("/v1/evolve", methods=["POST"])
def submit_evolution():
    """Submit an evolved driver variant for evaluation.

    Body (JSON):
        {
            "base_driver": "virtio_net",
            "base_version": "1.0.0",
            "arch": "x86_64",
            "optimization_type": "performance|size|reliability",
            "metrics": {
                "throughput_mbps": 950,
                "latency_us": 12,
                "code_size": 4096,
                "crash_count": 0
            },
            "description": "Optimized TX path: batch descriptors",
            "author": "claude-evolution-agent"
        }

    Optionally include a 'file' field with the evolved .ajdrv binary
    (multipart/form-data).

    Response:
        { "status": "submitted", "evolution_id": N }
    """
    if request.content_type and "json" in request.content_type:
        body = request.get_json(silent=True) or {}
        binary = None
    elif "file" in request.files:
        binary = request.files["file"].read()
        meta_json = request.form.get("metadata", "{}")
        try:
            body = json.loads(meta_json)
        except json.JSONDecodeError:
            body = {}
    else:
        return jsonify({"error": "Send JSON or multipart with 'file' + 'metadata'"}), 400

    entry = {
        "id": len(_evolution_log) + 1,
        "base_driver": body.get("base_driver", "unknown"),
        "base_version": body.get("base_version", "0.0.0"),
        "arch": body.get("arch", "x86_64"),
        "optimization_type": body.get("optimization_type", "performance"),
        "metrics": body.get("metrics", {}),
        "description": body.get("description", ""),
        "author": body.get("author", "anonymous"),
        "status": "pending_review",
        "has_binary": binary is not None,
    }
    _evolution_log.append(entry)

    # If binary provided, save it for review
    if binary:
        evo_dir = DRIVERS_DIR / "evolved"
        evo_dir.mkdir(parents=True, exist_ok=True)
        evo_file = evo_dir / f"evo_{entry['id']}_{entry['base_driver']}.ajdrv"
        evo_file.write_bytes(binary)

    return jsonify({"status": "submitted", "evolution_id": entry["id"]}), 201


@app.route("/v1/evolve", methods=["GET"])
def list_evolutions():
    """List submitted evolution entries.

    Query params:
        driver  -- filter by base driver name
        status  -- filter by status (pending_review, approved, rejected)

    Response:
        { "evolutions": [...], "total": N }
    """
    driver = request.args.get("driver")
    status_filter = request.args.get("status")

    results = _evolution_log
    if driver:
        results = [e for e in results if e["base_driver"] == driver]
    if status_filter:
        results = [e for e in results if e["status"] == status_filter]

    return jsonify({"evolutions": results, "total": len(results)})


# ── Community Audit / Review System ──

# In-memory review log (production would use a database)
_reviews = []

@app.route("/v1/reviews", methods=["GET"])
def list_reviews():
    """List driver reviews and audit entries.

    Query params:
        driver  -- filter by driver name
        status  -- filter by status (pending, approved, rejected)

    Response:
        { "reviews": [...], "total": N }
    """
    driver = request.args.get("driver")
    status_filter = request.args.get("status")

    results = _reviews
    if driver:
        results = [r for r in results if r["driver_name"] == driver]
    if status_filter:
        results = [r for r in results if r["status"] == status_filter]

    return jsonify({"reviews": results, "total": len(results)})


@app.route("/v1/reviews", methods=["POST"])
def submit_review():
    """Submit a review for a driver.

    Body (JSON):
        {
            "driver_name": "virtio_net",
            "version": "1.0.0",
            "arch": "x86_64",
            "reviewer": "community-auditor-1",
            "verdict": "approved|rejected|needs_changes",
            "comments": "Code reviewed, no security issues found.",
            "stability_score": 95,
            "security_score": 90,
            "performance_score": 85
        }

    Response:
        { "status": "recorded", "review_id": N }
    """
    body = request.get_json(silent=True)
    if body is None:
        return jsonify({"error": "Invalid JSON body"}), 400

    review = {
        "id": len(_reviews) + 1,
        "driver_name": body.get("driver_name", "unknown"),
        "version": body.get("version", "0.0.0"),
        "arch": body.get("arch", "x86_64"),
        "reviewer": body.get("reviewer", "anonymous"),
        "verdict": body.get("verdict", "pending"),
        "comments": body.get("comments", ""),
        "stability_score": body.get("stability_score", 0),
        "security_score": body.get("security_score", 0),
        "performance_score": body.get("performance_score", 0),
        "status": body.get("verdict", "pending"),
    }
    _reviews.append(review)

    # If driver is approved by enough reviewers, mark it as trusted in catalog
    driver_reviews = [r for r in _reviews
                      if r["driver_name"] == review["driver_name"]
                      and r["version"] == review["version"]
                      and r["status"] == "approved"]

    approval_count = len(driver_reviews)
    if approval_count >= 2:
        # Auto-promote to trusted status (would update catalog in production)
        pass

    return jsonify({
        "status": "recorded",
        "review_id": review["id"],
        "approval_count": approval_count,
    }), 201


@app.route("/v1/reviews/<int:review_id>", methods=["GET"])
def get_review(review_id: int):
    """Get a specific review by ID."""
    for r in _reviews:
        if r["id"] == review_id:
            return jsonify(r)
    return jsonify({"error": "Review not found"}), 404


@app.route("/v1/metrics", methods=["POST"])
def report_metrics():
    """Report driver performance/stability metrics from a running OS.

    Body (JSON):
        {
            "driver_name": "virtio_net",
            "version": "1.0.0",
            "arch": "x86_64",
            "uptime_hours": 24.5,
            "crash_count": 0,
            "tx_bytes": 1048576,
            "rx_bytes": 2097152,
            "error_count": 0
        }

    Response:
        { "status": "recorded" }
    """
    body = request.get_json(silent=True)
    if body is None:
        return jsonify({"error": "Invalid JSON body"}), 400

    # In production, this would be stored in a time-series database
    # for driver quality tracking and evolution fitness scoring
    return jsonify({"status": "recorded"})


# ── Error handlers ──

@app.errorhandler(404)
def not_found(e):
    return jsonify({"error": "Not found"}), 404


@app.errorhandler(405)
def method_not_allowed(e):
    return jsonify({"error": "Method not allowed"}), 405


@app.errorhandler(500)
def internal_error(e):
    return jsonify({"error": "Internal server error"}), 500


# ── Main ──

def main():
    host = os.environ.get("ALJEFRA_HOST", "0.0.0.0")
    port = int(os.environ.get("ALJEFRA_PORT", "8080"))
    debug = os.environ.get("ALJEFRA_DEBUG", "0") == "1"

    _auto_seed()

    print(f"AlJefra Driver Marketplace API")
    print(f"  Listening on {host}:{port}")
    print(f"  Catalog: {CATALOG_FILE}")
    print(f"  Drivers: {DRIVERS_DIR}/")
    print()

    app.run(host=host, port=port, debug=debug)


if __name__ == "__main__":
    main()
