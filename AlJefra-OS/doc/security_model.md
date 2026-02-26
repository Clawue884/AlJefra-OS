# AlJefra OS — Security Model

## Threat Model

AlJefra OS downloads and executes driver code from the internet. The security model must ensure:

1. **Driver integrity**: Downloaded drivers haven't been tampered with
2. **Driver authenticity**: Drivers come from trusted sources
3. **Transport security**: Network communication is encrypted
4. **Runtime isolation**: Drivers can't corrupt the kernel

## Code Signing

### Ed25519 Digital Signatures

Every `.ajdrv` package includes a 64-byte Ed25519 signature over the package contents (excluding the signature itself).

**Why Ed25519:**
- Small keys (32 bytes) and signatures (64 bytes)
- Fast verification (~70K ops/sec)
- No NIST dependency, no patents
- Deterministic (no RNG needed for signing)

### Trust Chain

```
AlJefra Root Key (offline, HSM)
  └→ Signs: Store Signing Key
       └→ Signs: Individual .ajdrv packages
```

The OS image ships with the Store Signing Key's public key (32 bytes).

### Verification Flow

```
1. Download .ajdrv from marketplace
2. Parse header, extract signature offset
3. Compute SHA-512 over signed region
4. Verify Ed25519 signature using Store public key
5. If valid: proceed to load driver
6. If invalid: reject, log error
```

## Transport Security

### TLS 1.2

All marketplace communication uses TLS 1.2 via BearSSL:
- Certificate chain validation against embedded root CAs
- ISRG Root X1, Amazon Root CA 1, DigiCert Global Root G2
- Compile-time date for X.509 validation

### Certificate Pinning

The marketplace domain (`api.aljefra.com`) uses certificate pinning:
- Pin the intermediate CA certificate
- Reject connections with unexpected certificates

## Runtime Isolation

### Current Model (Exokernel)

In the exokernel model, drivers run in the same address space as the kernel. Protection relies on:
- Code signing (only trusted code executes)
- Review process for marketplace submissions
- Community auditing

### Future Model (Planned)

Phase 5 will add:
- **Driver sandboxing**: Drivers run in isolated address spaces
- **Capability-based access**: Drivers can only access hardware they're authorized for
- **Watchdog timer**: Drivers that hang are terminated

## Secure Boot

### x86-64 UEFI Secure Boot
- Sign the bootloader with a UEFI key
- Pure64 verifies kernel signature
- Kernel verifies payload signature

### ARM64 Secure Boot
- Use platform-specific secure boot (e.g., ARM TrustZone)
- U-Boot verified boot with FIT images

### RISC-V
- OpenSBI verified boot (when available)

## Key Management

### AlJefra Root Key
- Generated offline on air-gapped machine
- Stored in HSM (Hardware Security Module)
- Used only to sign the Store Signing Key
- Rotated annually

### Store Signing Key
- Used to sign all `.ajdrv` packages
- Embedded in OS image (32-byte Ed25519 public key)
- Rotated quarterly, with overlap period

### Developer Keys
- Third-party developers sign their submissions
- Developer keys are registered with the marketplace
- Multi-signature support for critical drivers

## Audit and Review

### Community Audit System
1. Developer submits `.ajdrv` with source code
2. Automated analysis: static analysis, fuzzing
3. Community reviewers verify source matches binary
4. Minimum 2 approvals before publishing
5. All versions tracked in version control

### Incident Response
- Compromised packages can be revoked via CRL
- Emergency key rotation procedure documented
- Devices check revocation list on each boot (when online)
