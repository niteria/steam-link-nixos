{
  pkgs,
  armPkgs,
  kernel,
  rootNand,
  rootImage,
  runtime,
  rootModule ? "steamlink_marvell_internal_root.ko",
  initTransform ? null,
  stateNonce ? "",
  snapshot ? "",
  recoveryUsbSerial ? "",
  expectedBadBlocks ? 0,
  shutdownScript ? null,
  additionalModules ? [ ],
}:
let
  busybox = armPkgs.pkgsStatic.busybox;
  closure = pkgs.closureInfo {
    rootPaths = [
      busybox
      armPkgs.mtdutils
    ];
  };
in
pkgs.runCommand "steamlink-internal-initramfs"
  {
    nativeBuildInputs = [
      pkgs.cpio
      pkgs.gzip
      pkgs.python3
      pkgs.patchelf
    ];
  }
  ''
    mkdir -p "$out" tree/bin tree/nix/store tree/lib/modules/7.2.6 tree/dev tree/proc tree/sys tree/run tree/newroot
    export CLOSURE_PATHS=${closure}/store-paths BUSYBOX=${busybox}/bin/busybox UBIATTACH=${armPkgs.mtdutils}/bin/ubiattach
    export MODULES=${runtime}/kernel-modules/lib/modules/7.2.6 DEPENDENCY_REPORT="$out/dependencies.json"
    export ROOT_MODULE=${rootModule}
    export EXTRA_MODULES=${pkgs.lib.escapeShellArg (pkgs.lib.concatStringsSep " " additionalModules)}
    ${pkgs.lib.optionalString (
      shutdownScript != null
    ) "export UBIDETACH=${armPkgs.mtdutils}/bin/ubidetach"}
    python3 ${./prune-initramfs.py}
    for applet in sh mount umount mkdir cat grep seq sleep cp chmod ln sha256sum uname printf modprobe switch_root ${
      pkgs.lib.optionalString (shutdownScript != null) "rmmod reboot poweroff sort awk losetup sync"
    }; do
      ln -s ${busybox}/bin/busybox tree/bin/$applet
    done
    ln -s ${armPkgs.mtdutils}/bin/ubiattach tree/bin/ubiattach
    ${pkgs.lib.optionalString (shutdownScript != null) ''
      ln -s ${armPkgs.mtdutils}/bin/ubidetach tree/bin/ubidetach
      cp ${shutdownScript} tree/state-shutdown
      chmod 755 tree/state-shutdown
    ''}
    export ROOT_IMAGE=${rootImage} SYSTEM=${runtime}
    export STATE_NONCE=${pkgs.lib.escapeShellArg stateNonce}
    export ROOT_SNAPSHOT=${pkgs.lib.escapeShellArg snapshot}
    export RECOVERY_USB_SERIAL=${pkgs.lib.escapeShellArg recoveryUsbSerial}
    export EXPECTED_BAD_BLOCKS=${toString expectedBadBlocks}
    python3 - <<'PY'
    import json, os, pathlib
    script = pathlib.Path('${./init.sh}').read_text()
    ${pkgs.lib.optionalString (
      initTransform != null
    ) "exec(pathlib.Path('${initTransform}').read_text())"}
    receipt = json.loads(pathlib.Path(os.environ['ROOT_IMAGE'], 'measurement.json').read_text())
    script = script.replace('@ROOT_SHA256@', receipt['image_sha256']).replace('@SYSTEM@', os.environ['SYSTEM'])
    assert '@ROOT_SHA256@' not in script and '@SYSTEM@' not in script
    pathlib.Path('tree/init').write_text(script)
    pathlib.Path('tree/init').chmod(0o755)
    PY
    # Every archive member has deterministic root ownership and timestamp.
    find tree -exec touch -h -d @1 {} +
    (cd tree; find . -print0 | sort -z | cpio --null -o -H newc --owner=0:0 --reproducible) | gzip -n -9 > "$out/initramfs.gz"
    cp tree/init "$out/init"
    cp ${rootImage}/measurement.json "$out/root-measurement.json"
    printf '%s\n' ${busybox}/bin/busybox > "$out/test-shell"
    sha256sum "$out/initramfs.gz" > "$out/initramfs.sha256"
    # Reject missing dependencies and mismatched architecture/format in an offline audit.
    export ARCHIVE="$out/initramfs.gz" KCONFIG=${kernel.configfile}
    python3 ${./audit-initramfs.py}
  ''
