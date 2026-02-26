# AlJefra OS -- Release Process

This document describes how AlJefra OS versions are numbered, tested, built,
signed, and published.

---

## Semantic Versioning

AlJefra OS follows [Semantic Versioning 2.0.0](https://semver.org/):

```
MAJOR.MINOR.PATCH
```

| Component | Incremented When                                        |
|-----------|---------------------------------------------------------|
| MAJOR     | Incompatible API or ABI changes (driver_ops_t, HAL)     |
| MINOR     | New features, new drivers, new architecture support      |
| PATCH     | Bug fixes, security patches, documentation updates       |

**Pre-release tags:** `-alpha`, `-beta`, `-rc1`, `-rc2`, etc.

Examples: `1.0.0`, `1.1.0-beta`, `1.1.0-rc1`, `1.1.0`

---

## Branch Model

| Branch    | Purpose                                                  |
|-----------|----------------------------------------------------------|
| `main`    | Stable releases only. Tagged with version numbers.       |
| `dev`     | Active development. PRs merge here first.                |
| `release/X.Y` | Release preparation branch (cut from `dev`).        |
| `hotfix/X.Y.Z` | Emergency fixes applied to `main`.                |

### Normal Release Flow

```
dev  -->  release/1.1  -->  main  (tag: v1.1.0)
```

1. Cut `release/X.Y` from `dev`.
2. Only bug fixes go into the release branch.
3. Merge to `main` when ready. Tag with `vX.Y.Z`.
4. Merge `main` back to `dev` to pick up any release fixes.

### Hotfix Flow

```
main  -->  hotfix/1.0.1  -->  main  (tag: v1.0.1)
                          -->  dev   (cherry-pick)
```

---

## Release Checklist

Before tagging a release, complete every item:

### Build

- [ ] `make ARCH=x86_64` succeeds with zero warnings
- [ ] `make ARCH=aarch64` succeeds with zero warnings
- [ ] `make ARCH=riscv64` succeeds with zero warnings
- [ ] All three kernel binaries are under 256 KB

### Test

- [ ] x86-64 QEMU boot: reaches "AlJefra OS Ready" prompt
- [ ] ARM64 QEMU boot: reaches "AlJefra OS Ready" prompt
- [ ] RISC-V QEMU boot: reaches "AlJefra OS Ready" prompt
- [ ] Marketplace driver download + load works (x86-64)
- [ ] Ed25519 signature verification passes
- [ ] Network stack (DHCP, DNS, HTTP) functional

### Documentation

- [ ] CHANGELOG.md updated with all changes since last release
- [ ] ROADMAP.md updated with completed items
- [ ] Version string in kernel updated (`sysvar.asm` or equivalent)
- [ ] Website download page updated

### Sign and Package

- [ ] Kernel binaries signed with Ed25519 release key
- [ ] ISO image built (GRUB2 bootloader)
- [ ] USB image built
- [ ] SHA-256 checksums generated for all artifacts

---

## Changelog Format

The CHANGELOG.md follows [Keep a Changelog](https://keepachangelog.com/):

```markdown
## [1.1.0] - 2026-MM-DD

### Added
- New feature description

### Changed
- Modified behavior description

### Fixed
- Bug fix description

### Removed
- Removed feature description

### Security
- Security fix description
```

---

## Binary Signing

All release binaries are signed with the AlJefra Release Key (Ed25519).

### Signing Steps

1. Build the release artifacts:
   ```bash
   make all-arch
   ```

2. Generate SHA-256 checksums:
   ```bash
   sha256sum build/*/bin/kernel_*.bin > SHA256SUMS
   ```

3. Sign the checksum file:
   ```bash
   python3 server/sign_tool.py sign \
     --key release_key.sec \
     --input SHA256SUMS
   ```

4. Publish: `SHA256SUMS`, `SHA256SUMS.sig`, and all kernel binaries.

### Verification

Users verify downloads with:

```bash
python3 server/sign_tool.py verify \
  --key release_key.pub \
  --input SHA256SUMS
sha256sum -c SHA256SUMS
```

---

## Distribution Channels

| Channel           | URL / Location                          | Content              |
|-------------------|-----------------------------------------|----------------------|
| GitHub Releases   | github.com/QatarIT/AlJefra-OS/releases  | Binaries + source    |
| Website           | os.aljefra.com/download                  | ISO, USB images      |
| Marketplace       | os.aljefra.com/marketplace               | .ajdrv driver pkgs   |

### GitHub Release Notes Template

```markdown
# AlJefra OS vX.Y.Z

**Release date:** YYYY-MM-DD

## Highlights
- Bullet point summary of major changes

## Downloads
| File | Architecture | Size | SHA-256 |
|------|-------------|------|---------|
| kernel_x86_64.bin | x86-64 | XXX KB | abc123... |
| kernel_aarch64.bin | ARM64 | XXX KB | def456... |
| kernel_riscv64.bin | RISC-V | XXX KB | ghi789... |
| aljefra-os-vX.Y.Z.iso | x86-64 | X.X MB | jkl012... |

## Full Changelog
See [CHANGELOG.md](CHANGELOG.md) for details.
```

---

## Post-Release

1. Announce on os.aljefra.com and GitHub.
2. Update the marketplace server to serve the new version.
3. Monitor GitHub issues for regression reports.
4. Begin `dev` work on the next version.

---

*AlJefra OS -- Built in Qatar. Built for the world.*
*Qatar IT -- www.QatarIT.com*
