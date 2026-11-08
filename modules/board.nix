{
  config,
  lib,
  pkgs,
  modulesPath,
  steamlink-reference,
  ...
}:
let
  cfg = config.hardware.steamlink;
  platform = import ../pkgs { inherit pkgs lib steamlink-reference; };
  native = cfg.storage.mode == "reclaimed";
  nonce = if cfg.storage.stateNonce == null then "0000000000000000" else cfg.storage.stateNonce;
  snapshot =
    if cfg.storage.snapshot == null then
      lib.concatStrings (lib.replicate 64 "0")
    else
      cfg.storage.snapshot;
  seedHash =
    if cfg.storage.seedImageHash == null then
      lib.concatStrings (lib.replicate 64 "0")
    else
      cfg.storage.seedImageHash;
  checkRoot = pkgs.writeShellScript "steamlink-check-reclaimed-root" (
    ''
      driver=steamlink_marvell_reclaim
    ''
    +
      builtins.replaceStrings
        [ "@BAD_BLOCK_COUNT@" "@STATE_NONCE@" ]
        [ (toString cfg.storage.expectedBadBlocks) nonce ]
        (builtins.readFile ../internal/reclaim/check-root.sh)
  );
in
{
  imports = [ "${modulesPath}/profiles/minimal.nix" ];

  options.hardware.steamlink = {
    enable = lib.mkEnableOption "Valve Steam Link 1003 board support" // {
      default = true;
    };
    packages = lib.mkOption {
      type = lib.types.raw;
      readOnly = true;
      internal = true;
    };
    rootCheck = lib.mkOption {
      type = lib.types.raw;
      readOnly = true;
      internal = true;
      description = "Enrolled native-root admission helper, shared with the media console.";
    };
    ethernetAddress = lib.mkOption {
      type = lib.types.nullOr (lib.types.strMatching "([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}");
      default = null;
      description = "This board's recovered Ethernet MAC; needed when building its bootstrap DTB.";
    };
    storage = {
      mode = lib.mkOption {
        type = lib.types.enum [
          "usb"
          "reclaimed"
        ];
        default = "usb";
        description = "USB root with read-only NAND, or an already enrolled expanded UBIFS root. This does not provision or format NAND.";
      };
      snapshot = lib.mkOption {
        type = lib.types.nullOr (lib.types.strMatching "[0-9a-f]{64}");
        default = null;
        description = "Public enrollment snapshot identifying the existing root marker.";
      };
      stateNonce = lib.mkOption {
        type = lib.types.nullOr (lib.types.strMatching "[0-9a-f]{16}");
        default = null;
        description = "Existing NAND ownership nonce; supplied by the machine configuration.";
      };
      seedImageHash = lib.mkOption {
        type = lib.types.nullOr (lib.types.strMatching "[0-9a-f]{64}");
        default = null;
        description = "Original mutable-root seed provenance, not a checksum of current filesystem contents.";
      };
      expectedBadBlocks = lib.mkOption {
        type = lib.types.ints.between 0 4096;
        default = 0;
      };
      recoveryUsbSerial = lib.mkOption {
        type = lib.types.nullOr (lib.types.strMatching "[A-Za-z0-9._-]{1,64}");
        default = null;
        description = "Qualified recovery drive identity. Null disables automatic USB recovery reboot/receipts.";
      };
    };
  };

  config = lib.mkIf cfg.enable {
    hardware.steamlink.packages = platform;
    hardware.steamlink.rootCheck = checkRoot;
    assertions = [
      {
        assertion = !native || cfg.storage.snapshot != null;
        message = "Reclaimed NAND requires the machine's enrollment snapshot.";
      }
      {
        assertion = !native || cfg.storage.stateNonce != null;
        message = "Reclaimed NAND requires the machine's ownership nonce.";
      }
      {
        assertion = !native || cfg.storage.seedImageHash != null;
        message = "Reclaimed NAND requires the machine's original seed provenance.";
      }
      {
        assertion = !native || cfg.ethernetAddress != null;
        message = "Reclaimed NAND bootstrap requires the machine's factory Ethernet identity.";
      }
    ];
    nixpkgs.buildPlatform = lib.mkDefault "x86_64-linux";
    nixpkgs.hostPlatform = "armv7l-linux";
    nixpkgs.config.allowUnsupportedSystem = true;
    boot.loader.grub.enable = false;
    boot.loader.generic-extlinux-compatible.enable = false;
    boot.initrd.enable = false;
    boot.kernelPackages = lib.mkDefault (pkgs.linuxPackagesFor platform.kernel);
    # The bootstrap container supplies the actual command line; this records it
    # without NixOS defaults that the qualified boot never receives.
    boot.kernelParams = lib.mkForce [
      "console=ttyS0,115200n8"
      "console=tty0"
      "fbcon=map:0"
      "usbcore.autosuspend=-1"
      "net.ifnames=0"
    ];
    boot.blacklistedKernelModules = [ "etnaviv" ];
    boot.extraModulePackages =
      if native then
        [
          platform.reclaimed
          platform.pool
        ]
      else
        [ platform.readonly ];
    # The native root loads its drivers from the bootstrap initramfs; the generic
    # NixOS defaults (atkbd, loop) are absent or built into this kernel.
    boot.kernelModules = if native then lib.mkForce [ ] else [ "steamlink_nand_overlay" ];
    fileSystems."/" =
      if native then
        {
          device = "ubi0:root-b";
          fsType = "ubifs";
          options = [
            "rw"
            "noatime"
            "compr=zstd"
          ];
        }
      else
        {
          device = "/dev/disk/by-label/steamlink-nixos";
          fsType = "ext3";
        };
    fileSystems."/boot" =
      if native then
        {
          device = "tmpfs";
          fsType = "tmpfs";
          options = [ "size=1M" ];
        }
      else
        {
          device = "/dev/disk/by-label/STEAMLINK";
          fsType = "vfat";
          options = [
            "nofail"
            "umask=0077"
          ];
        };
    boot.nixStoreMountOpts = lib.mkIf native [
      "rw"
      "nosuid"
      "nodev"
    ];
    boot.tmp.useTmpfs = lib.mkDefault native;
    systemd.shutdownRamfs.enable = lib.mkIf native false;
    services.journald.settings.Journal.Storage = lib.mkIf native (lib.mkDefault "volatile");
    systemd.services.steamlink-writable-root-check = lib.mkIf native {
      description = "Verify the native writable UBIFS root and RAM shutdown environment";
      wantedBy = [ "multi-user.target" ];
      after = [ "local-fs.target" ];
      before = [ "sshd.service" ];
      path = [ pkgs.util-linux ];
      serviceConfig.Type = "oneshot";
      script = "exec ${checkRoot}";
    };
    system.build.steamlinkBootInputs = lib.mkIf native (
      import ../pkgs/bootstrap.nix {
        inherit
          pkgs
          platform
          nonce
          snapshot
          seedHash
          ;
        runtime = config.system.build.toplevel;
        ethernetAddress = cfg.ethernetAddress;
        expectedBadBlocks = cfg.storage.expectedBadBlocks;
        recoveryUsbSerial = cfg.storage.recoveryUsbSerial;
      }
    );
    # A freshly seeded root image is populated offline; register those store
    # paths before the daemon is used. Ordering against the socket would cycle.
    systemd.services.steamlink-register-store = {
      description = "Register the offline-populated Nix store";
      wantedBy = [ "multi-user.target" ];
      before = [ "nix-daemon.service" ];
      after = [ "local-fs.target" ];
      unitConfig.ConditionPathExists = "/nix-path-registration";
      serviceConfig.Type = "oneshot";
      script = ''
        ${config.nix.package}/bin/nix-store --store local --load-db < /nix-path-registration
        rm /nix-path-registration
      '';
    };
    networking.useDHCP = lib.mkDefault false;
    networking.useNetworkd = lib.mkDefault true;
    networking.nftables.enable = lib.mkDefault true;
    networking.firewall.enable = lib.mkDefault true;
    systemd.network.wait-online.enable = lib.mkDefault false;
    systemd.network.networks."10-ethernet" = {
      matchConfig.Name = "eth*";
      networkConfig = {
        DHCP = "yes";
        IPv6AcceptRA = true;
      };
    };
    nix.settings.max-jobs = lib.mkDefault 0;
    nix.settings.experimental-features = [
      "nix-command"
      "flakes"
    ];
    nixpkgs.flake.setNixPath = lib.mkDefault false;
    nixpkgs.flake.setFlakeRegistry = lib.mkDefault false;
    system.disableInstallerTools = lib.mkDefault true;
    environment.defaultPackages = lib.mkDefault [ ];
    environment.systemPackages = [ pkgs.usbutils ];
    documentation.enable = lib.mkDefault false;
    programs.command-not-found.enable = lib.mkDefault false;
    programs.nano.enable = lib.mkDefault false;
    programs.bash.completion.enable = lib.mkDefault false;
    zramSwap.enable = lib.mkDefault true;
    i18n.supportedLocales = lib.mkDefault [ "C.UTF-8/UTF-8" ];
    i18n.defaultLocale = lib.mkDefault "C.UTF-8";
    services.journald.settings.Journal = {
      SystemMaxUse = lib.mkDefault "32M";
      RuntimeMaxUse = lib.mkDefault "16M";
    };
  };
}
