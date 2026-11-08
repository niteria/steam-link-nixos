# Provenance

This repository holds the reusable platform support from a private bring-up of a
Valve Steam Link 1003. It was published as a fresh history.

Host-specific values are consumer options: Ethernet and radio MACs, root-marker
snapshot, ownership nonce, recovery USB serial, expected bad blocks, users and
keys, hostname and advertised LAN subnets. Tests use synthetic values.

Boot containers, NAND and root backups, credentials, captured logs and vendor AV
binaries are not included. The proprietary vendor runtime stays on the device.
Valve SDK headers and old-ABI runtime files, including `libpthread-2.19.so`, are
fetched from the public `steamlink-sdk` by hash.

## Third-party material

- The Linux kernel and its Marvell NAND driver are obtained from pinned nixpkgs
  sources; the driver build applies the retained platform patches. Linux licensing
  and per-file notices apply.
- `steamlink-reference` pins Ondra-Zik's Steam Link support repository for the
  board defconfig and wireless firmware. Its upstream notices and firmware terms
  apply to those fetched inputs.
- Retained C/header/patch files keep their SPDX identifiers and copyright notices.
  They include GPL-2.0-only kernel code and MIT-marked helpers/tests. This document
  does not replace those individual licenses or relicense third-party material.
