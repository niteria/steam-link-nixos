# steam-link-nixos

Reusable NixOS platform support for the **Valve Steam Link 1003** (Berlin2CD,
ARMv7). The package set is pinned to the kernel and early NAND stack qualified
by the bring-up work. Machine identity and service policy belong to the consumer.

## Consumer interface

Use `lib.mkSystem` to instantiate the board with this repository's independently
pinned `platform-nixpkgs`. This avoids accidentally changing the kernel ABI when a
desktop/fleet input is updated:

```nix
inputs.steam-link-nixos.url = "github:niteria/steam-link-nixos/PINNED_REV";

nixosConfigurations.my-steamlink = inputs.steam-link-nixos.lib.mkSystem {
  modules = [ ./hosts/my-steamlink ];
};
```

`nixosModules.default` / `nixosModules.steamlink` expose the modules for consumers
that arrange the same compatible NixOS/package-set environment themselves.
`nixosModules.tailscaleRouter` exposes the routing compatibility module separately.

The defaults describe a USB root and read-only NAND. An **already enrolled**
expanded UBIFS installation opts into `hardware.steamlink.storage.mode = "reclaimed"`
and supplies its existing snapshot, ownership nonce, seed provenance, expected bad
block count, Ethernet MAC and optional qualified recovery-drive serial. These
options describe existing ownership; they do not create, resize or format volumes.

Relevant options:

| Option | Consumer responsibility |
|---|---|
| `hardware.steamlink.ethernetAddress` | Recovered factory Ethernet identity |
| `hardware.steamlink.storage.mode` | `usb` or already installed `reclaimed` layout |
| `hardware.steamlink.storage.snapshot` | Existing root-marker enrollment snapshot |
| `hardware.steamlink.storage.stateNonce` | Existing 16-digit hexadecimal ownership nonce |
| `hardware.steamlink.storage.seedImageHash` | Original seed provenance, not live-root contents |
| `hardware.steamlink.storage.expectedBadBlocks` | Observed factory bad-block count |
| `hardware.steamlink.storage.recoveryUsbSerial` | Qualified drive; null disables automatic recovery reboot |
| `services.steamlinkWifi.enable` | Opt-in Wi-Fi association using a runtime credential file |
| `services.steamlinkRadioIdentity.*` | Factory Wi-Fi/Bluetooth addresses, when enabled |
| `services.steamlinkRelay.*` | Explicit IPv4 LAN subnets, interfaces and route-acceptance policy |
| `hardware.steamlink.media.*` | Opt-in vendor PE/HDMI stack: AV kernel modules, framebuffer console, diagnostics, on-demand desktop |
| `hardware.steamlink.hdmiAudio.enable` | Opt-in PCM sink playing a FIFO on the TV |
| `hardware.steamlink.hdmiAudio.rate` | HDMI stream rate: 44100, or 48000 resampled in the sink |

The consumer also configures hostname, users, public SSH keys, sudo policy,
OpenSSH and `system.stateVersion`. No user account, authorized key, LAN subnet,
board-specific MAC, enrollment nonce or private credential is supplied by default.

## Platform and bootstrap

- Cross-build host: x86_64 Linux; target: ARMv7 hard-float.
- Qualified Linux 7.2.6 configuration with the installed framebuffer/Etnaviv ABI.
  Without `hardware.steamlink.media.enable` no graphics or vendor-media service is
  installed. With it, the SHM/CC, PE, TWSI and GPIO bridge modules, the PE-hosting
  framebuffer console, its bounded PE log, local diagnostics and the optional
  Openbox desktop proof are added. The proprietary vendor runtime is not in Nix; the
  console starts only when the device's verified `/opt/steamlink-vendor` is present.
- Hardened read-only NAND, factory-BBT handling and guarded lower-page state
  writer, with original opcode/ECC/bounds/state tests retained.
- Writer faults are classified and logged. Controller/transport faults still stop
  all writes; a media failure (NAND status FAIL, non-erased target or upper page,
  read-back mismatch) fails only that operation with `-EIO`, so UBI relocates the
  data. UBI may retire a block for the current boot; NAND bad-block tables are
  never written. Counters: `media_faults`, `retired_blocks`, `writes_faulted`.
- Expanded pool order: original cache, then sysconf, then factory. Protected
  boot/identity/recovery/root/BBT gaps are excluded from the writer.
- Explicit `ubiattach -b 60 --disable-fastmap`, with at least 160 bad-block spares.
- Existing root A/state geometry, root B marker/ownership admission, dynamic Nix
  system-profile selection and RAM-resident clean unmount/detach/writer shutdown.

For an enrolled native-root system,
`config.system.build.steamlinkBootInputs` produces the public kernel, DTB,
initramfs and measurement metadata. The DTB preserves the qualified low-memory,
192 MiB reusable AV pool and 16 MiB descriptor layout. It takes the Ethernet identity
from the consumer. The output contains **no SSH private key**; private identity
addition and signed-stock handoff/container publication remain separate operator
steps. Initial flashing and recovery use the enrollment's separately retained
capsules and proofs, rather than ordinary `nixos-rebuild` bootloader installation.

The current physical layout implementation is specific to the qualified 4 GiB
Steam Link geometry. This is support for that board/layout, not a general NAND
partition manager. Existing installations must retain their ownership values and
use a separately qualified bootstrap update for kernel/DTB/early-driver changes.

## Optional relay role

```nix
services.steamlinkRelay = {
  enable = true;
  subnets = [ "10.44.0.0/24" ];
  lanInterfaces = [ "eth0" ];
  acceptRoutes = true;
};
```

The qualified kernel lacks IPv4 policy-routing tables and connection marks.
Tailscale uses its main-table fallback and supported externally managed firewall
mode. NixOS nftables supplies explicit LAN forwarding/SNAT and off-tunnel spoof
protection. Tailnet peer ACLs remain enforced. This module does not authorize a
node or approve its routes; enrollment state stays in the runtime state directory.

## Optional HDMI audio sink

```nix
hardware.steamlink.media.enable = true;
systemd.services.steamlink-framebuffer-console.enable = false;
hardware.steamlink.hdmiAudio.enable = true;
```

`hardware.steamlink.hdmiAudio.enable` plays audio on the TV over HDMI with no
display client. Applications write to `/run/steamlink-pcm/hdmi` from the
`steamlink-pcm` group, one writer at a time.

- `steamlink-audio-pe` hosts the vendor PE headless. The verified vendor tree is
  bind-mounted read-only into a 1 MiB tmpfs root rather than copied into RAM.
  Readiness is PE's own `/data/.PE_AV_Init.done` FIFO, created once its RPC
  service is installed.
- `steamlink-pcm-sink` reads S16LE stereo from `/run/steamlink-pcm/hdmi` and
  submits 50 ms chunks through SLAudio, keeping at most 300 ms queued and
  sleeping 5–50 ms between queue queries. Each writer session is padded and
  followed by 100 ms of silence, because SLAudio has no flush. The stream is
  freed after 15 s idle. `hdmiAudio.rate = 48000` resamples 44.1 kHz in the sink.

**The TV must be connected when the Steam Link boots.** PE detects a TV plugged
in later, but its HDMI audio then stays silent, and PE cannot be restarted in
the same boot.

PE is started once per boot. A warm vendor re-initialization stalls early in
PE's startup, so the host refuses a second PE start in the same boot, and a
configuration change applies to it at the next boot. The sink and the relay
restart freely; PE frees a killed client's stream.

Measured on the 1003 with the TV path at 44.1 kHz (drained in real time) and
the qualified 128 MiB AV cache: PE holds about 15 MiB RSS and uses about 8% CPU
idle and 16% while playing. Its audio and display tasks run SCHED_RR. The sink
uses about 1.3%.

## Checks and updates

```sh
nix flake check
nix build .#checks.x86_64-linux.qualified-artifacts --no-link
```

Checks cover NAND opcode/BBT/ECC/SLC/state/layout contracts, synthetic device
identities, invalid enrollment data, absent recovery media, IPv4 subnet validation,
unsigned 64-bit nonces, shutdown ordering and the qualified kernel/module hashes.
The read-only example contains no credentials or personal machine values.

Consumers pin a published commit.
To update, change that revision in the consumer and run
`nix flake update steam-link-nixos`, then build its host and bootstrap outputs.
Evaluating or building these outputs does not activate a running machine.

See [PROVENANCE.md](PROVENANCE.md) for sources and licensing notes.

## License

MIT for this project's own code; files with an SPDX identifier and the kernel
patches under `nand/` keep their own terms. See [LICENSE](LICENSE).
