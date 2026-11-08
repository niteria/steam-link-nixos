{
  pkgs,
  platform,
  runtime,
  nonce,
  snapshot,
  seedHash,
  ethernetAddress,
  expectedBadBlocks,
  recoveryUsbSerial,
}:
let
  host = pkgs.buildPackages;
  base = import ../av/reserved-memory.nix {
    pkgs = host;
    inherit ethernetAddress;
    inherit (platform) kernel;
    withPe = true;
    reserveLow = true;
  };
  graphics = import ../av/etnaviv/dtb.nix {
    pkgs = host;
    inherit base;
  };
  shared = import ../av/kodi/shared-dtb.nix {
    pkgs = host;
    base = graphics;
  };
  dtb = host.runCommand "steamlink-descriptor-16-dtb" { nativeBuildInputs = [ host.dtc ]; } ''
    mkdir -p "$out"
    cp ${shared}/berlin2cd-av-reserved.dtb "$out/berlin2cd-av-reserved.dtb"
    chmod u+w "$out/berlin2cd-av-reserved.dtb"
    fdtput -t x "$out/berlin2cd-av-reserved.dtb" /reserved-memory/av-noncache@1e000000 reg 1e000000 1000000
    test "$(fdtget -t x "$out/berlin2cd-av-reserved.dtb" /reserved-memory/av-noncache@1e000000 reg)" = '1e000000 1000000'
    fdtget "$out/berlin2cd-av-reserved.dtb" /reserved-memory/av-noncache@1e000000 no-map
    test "$(fdtget -t x "$out/berlin2cd-av-reserved.dtb" /reserved-memory/av-cache@12000000 reg)" = '12000000 c000000'
    dtc -I dtb -O dts "$out/berlin2cd-av-reserved.dtb" > "$out/decoded.dts"
  '';
  rootMetadata = host.writeTextDir "measurement.json" (
    builtins.toJSON {
      filesystem = "ubifs";
      image_sha256 = seedHash;
      image_sha256_scope = "original seed, not current mutable filesystem";
      runtime_system = toString runtime;
      existing_mutable_root = true;
    }
  );
  initTransform = host.writeText "steamlink-reclaimed-root-init.py" (
    builtins.readFile ../internal/writable/init-transform.py
    + "\n"
    + builtins.readFile ../internal/reclaim/init-transform.py
  );
  shutdownBase =
    builtins.replaceStrings
      [ "ubidetach -m 0 || hold 'UBI detach'" "steamlink_marvell_state" "steamlink_state_vm" ]
      [
        "ubidetach -d 0 || hold 'UBI detach'\nrmmod steamlink_reclaim_pool || hold 'pool unload'"
        "steamlink_marvell_reclaim"
        "steamlink_reclaim_vm"
      ]
      (builtins.readFile ../internal/state/shutdown.sh);
  shutdown = host.writeText "steamlink-reclaimed-root-shutdown" (
    if recoveryUsbSerial != null then
      builtins.replaceStrings [ "@RECOVERY_USB_SERIAL@" ] [ recoveryUsbSerial ] shutdownBase
    else
      builtins.replaceStrings
        [
          ''[ "$(cat /sys/block/sda/device/../../../../serial)" = @RECOVERY_USB_SERIAL@ ] || hold receipt-drive''
        ]
        [ "    hold receipt-drive-not-configured" ]
        shutdownBase
  );
  initramfs = import ../internal/initramfs.nix {
    pkgs = host;
    armPkgs = pkgs;
    inherit
      runtime
      initTransform
      snapshot
      expectedBadBlocks
      ;
    inherit (platform) kernel;
    rootNand = platform.reclaimed;
    rootImage = rootMetadata;
    rootModule = "steamlink_marvell_reclaim.ko";
    stateNonce = nonce;
    recoveryUsbSerial = if recoveryUsbSerial == null then "" else recoveryUsbSerial;
    shutdownScript = shutdown;
    additionalModules = [
      "vfb.ko"
      "steamlink_reclaim_pool.ko"
    ];
  };
in
host.linkFarm "steamlink-existing-root-boot-inputs" [
  {
    name = "root";
    path = rootMetadata;
  }
  {
    name = "initramfs";
    path = initramfs;
  }
  {
    name = "kernel";
    path = platform.kernel;
  }
  {
    name = "dtb";
    path = dtb;
  }
]
