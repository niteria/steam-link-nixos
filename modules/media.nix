{
  config,
  lib,
  pkgs,
  steamlinkBuildPkgs,
  ...
}:
let
  board = config.hardware.steamlink;
  cfg = board.media;
  native = board.storage.mode == "reclaimed";
  host = steamlinkBuildPkgs;
  k = config.boot.kernelPackages;
  # Public Valve SDK ABI files; the proprietary vendor runtime stays on the device.
  stockRuntime = import ../av/stock-runtime.nix { pkgs = host; };
  presenter = import ../console/presenter.nix {
    pkgs = host;
    armPkgs = pkgs;
    runtime = stockRuntime;
  };
  diagnostics = import ../console/diagnostics.nix {
    pkgs = host;
    armPkgs = pkgs;
  };
  shmcc = k.callPackage ../av/compat/module.nix { };
  pe = k.callPackage ../av/pe/module.nix {
    sdk = import ../av/pe/sdk.nix { pkgs = host; };
    inherit shmcc;
  };
  twsi = k.callPackage ../av/pe/twsi.nix { busFrequency = 100000; };
  gpio = k.callPackage ../av/pe/gpio.nix { };
  logCap = pkgs.pkgsStatic.callPackage ../console/log-cap.nix { };
  capPeLog = pkgs.writeShellScript "steamlink-cap-pe-log" ''
    read -r pid < /run/steamlink-console/pe.pid
    exec ${logCap}/bin/log-cap "$pid" /run/steamlink-console/pe.log 2097152 --watch
  '';
  snapshot = pkgs.writeShellScriptBin "steamlink-fb-snapshot" ''
    exec ${stockRuntime}/lib/ld-linux-armhf.so.3 --library-path ${stockRuntime}/lib \
      ${presenter}/bin/fb-console --snapshot /dev/fb0
  '';
  desktopSession = import ../desktop/default.nix { inherit pkgs; };
in
{
  options.hardware.steamlink.media = {
    enable = lib.mkEnableOption "the vendor PE/HDMI media stack, framebuffer console and diagnostics";
    cacheMiB = lib.mkOption {
      type = lib.types.addCheck lib.types.int (
        value: value >= 64 && value <= 192 && lib.mod value 4 == 0
      );
      default = 128;
      description = "Cached AV DMA bank exposed by the SHM/CC bridge.";
    };
    hdmiMode = lib.mkOption {
      type = lib.types.nullOr (
        lib.types.enum [
          "720p"
          "1080p"
        ]
      );
      default = null;
      description = "Startup output mode requested from PE; null keeps its EDID-based selection.";
    };
    desktop.enable = lib.mkEnableOption "the on-demand Openbox desktop proof on tty8";
  };

  config = lib.mkMerge [
    (lib.mkIf (cfg.enable && cfg.desktop.enable) {
      environment.systemPackages = [ desktopSession ];
      # On demand; it takes over tty8 and returns to tty1 when stopped.
      systemd.services.steamlink-desktop = {
        description = "Lightweight Steam Link desktop from internal NAND";
        after = [ "steamlink-framebuffer-console.service" ];
        requires = [ "steamlink-framebuffer-console.service" ];
        bindsTo = [ "steamlink-framebuffer-console.service" ];
        serviceConfig = {
          Type = "notify";
          NotifyAccess = "all";
          ExecStart = "${desktopSession}/bin/steamlink-desktop-proof";
          ExecStopPost = "${pkgs.kbd}/bin/chvt 1";
          TTYPath = "/dev/tty8";
          StandardInput = "tty-force";
          TTYReset = true;
          TimeoutStartSec = 150;
          TimeoutStopSec = 20;
          KillMode = "control-group";
        };
      };
    })
    (lib.mkIf cfg.enable {
      # Loaded after the platform NAND/pool modules.
      boot.extraModulePackages = lib.mkAfter [
        shmcc
        pe
        twsi
        gpio
      ];
      boot.extraModprobeConfig = ''
        options steamlink_av_shmcc cache_dma_mib=${toString cfg.cacheMiB}
      '';
      environment.systemPackages = [
        snapshot
        pkgs.kbd
        diagnostics
        (pkgs.writeShellScriptBin "steamlink-audio-check" ''exec ${diagnostics}/bin/steamlink-diagnostics channels "$@"'')
        (pkgs.writeShellScriptBin "steamlink-av-sync" ''exec ${diagnostics}/bin/steamlink-diagnostics sync "$@"'')
      ];
      # On demand from the console menu rather than at boot.
      systemd.services.steamlink-local-diagnostics = {
        description = "Local offline Steam Link diagnostic menu on tty9";
        after = [ "steamlink-framebuffer-console.service" ];
        unitConfig.ConditionPathExists = "/run/steamlink-console/root/run/media-player/ready";
        serviceConfig = {
          ExecStart = "${diagnostics}/bin/steamlink-diagnostics menu";
          TTYPath = "/dev/tty9";
          StandardInput = "tty-force";
          StandardOutput = "tty";
          StandardError = "tty";
          TTYReset = true;
          Restart = "always";
          RestartSec = 2;
        };
      };
      services.getty.extraArgs = [ "--noclear" ];
      services.getty.greetingLine = "NixOS Steam Link - NAND framebuffer console";
      systemd.services.steamlink-pe-log-cap = {
        description = "Bound native PE diagnostics in RAM";
        wantedBy = [ "steamlink-framebuffer-console.service" ];
        after = [ "steamlink-framebuffer-console.service" ];
        partOf = [ "steamlink-framebuffer-console.service" ];
        bindsTo = [ "steamlink-framebuffer-console.service" ];
        unitConfig.ConditionPathExists = "/run/steamlink-console/pe.pid";
        serviceConfig = {
          Type = "exec";
          ExecStart = capPeLog;
          Nice = 10;
          MemoryMax = "8M";
          Restart = "on-failure";
          RestartSec = 30;
        };
      };
      systemd.services.steamlink-framebuffer-console = {
        description = "Present the Linux framebuffer console on Steam Link HDMI";
        wantedBy = [ "multi-user.target" ];
        after = [
          "local-fs.target"
          "sshd.service"
          "systemd-udev-settle.service"
        ];
        wants = [ "sshd.service" ];
        unitConfig = {
          ConditionKernelCommandLine = "steamlink.console";
          ConditionPathExists =
            if native then
              "/opt/steamlink-vendor.sha256"
            else
              "/run/steamlink/lower/opt/steamlink-vendor.sha256";
        };
        path = [
          pkgs.coreutils
          pkgs.util-linux
          pkgs.kmod
          pkgs.systemd
          pkgs.procps
          pkgs.kbd
          pkgs.gawk
        ];
        serviceConfig = {
          Type = "notify";
          NotifyAccess = "all";
          ExecStart = "${pkgs.runtimeShell} ${../console/run.sh} ${presenter}/bin/fb-console";
          TimeoutStartSec = 240;
          TimeoutStopSec = 10;
          KillMode = "control-group";
        };
        environment = {
          STEAMLINK_DIAGNOSTICS = "${diagnostics}/share/steamlink-diagnostics";
        }
        // lib.optionalAttrs native {
          STEAMLINK_ROOT_MODE = "ubifs-reclaimed";
          STEAMLINK_VENDOR_ROOT = "/opt/steamlink-vendor";
          STEAMLINK_VENDOR_SUMS = "/opt/steamlink-vendor.sha256";
          STEAMLINK_RECLAIM_CHECK = toString board.rootCheck;
        }
        // lib.optionalAttrs (cfg.hdmiMode != null) {
          # libDisplay's EDID-aware startup selection; must reach PE before init.
          PE_USER_DEFAULT_RESOLUTION = cfg.hdmiMode;
        };
      };
      systemd.services.steamlink-writable-root-check.before = lib.mkIf native [
        "steamlink-framebuffer-console.service"
      ];
    })
  ];
}
