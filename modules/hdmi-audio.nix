{
  config,
  lib,
  pkgs,
  steamlinkBuildPkgs,
  ...
}:
let
  board = config.hardware.steamlink;
  audio = board.hdmiAudio;
  sinkEnabled = board.hdmiAudio.enable;
  native = board.storage.mode == "reclaimed";
  host = steamlinkBuildPkgs;
  stockRuntime = import ../av/stock-runtime.nix { pkgs = host; };
  pcmSink = import ../av/audio/pcm-sink.nix {
    pkgs = host;
    armPkgs = pkgs;
    runtime = stockRuntime;
  };
  root = "/run/steamlink-audio/root";
  pcmDir = "/run/steamlink-pcm";
  fifo = audio.fifo;
  vendorRun =
    command:
    "${pkgs.coreutils}/bin/chroot ${root} /bin/busybox sh -c "
    + lib.escapeShellArg "export LD_LIBRARY_PATH=/home/steam/lib:/usr/lib:/lib ALSA_CONFIG_PATH=/run/alsa.conf; exec /lib/ld-linux-armhf.so.3 ${command}";
in
{
  options.hardware.steamlink.hdmiAudio = {
    enable = lib.mkEnableOption "the vendor PE host, PCM sink and FIFO that play audio on the TV";
    rate = lib.mkOption {
      type = lib.types.enum [
        44100
        48000
      ];
      default = 44100;
      description = ''
        Sample rate of the vendor SLAudio stream. Sources write 44.1 kHz;
        48000 resamples it in the sink (160/147 polyphase) for the 48 kHz path
        that the earlier HDMI audio work qualified.
      '';
    };
    fifo = lib.mkOption {
      type = lib.types.str;
      default = "/run/steamlink-pcm/hdmi";
      readOnly = true;
      description = "FIFO taking 44.1 kHz S16LE stereo for the HDMI sink; one writer at a time.";
    };
  };

  config = lib.mkIf sinkEnabled {
    assertions = [
      {
        assertion = board.media.enable;
        message = "HDMI audio sources need hardware.steamlink.media.enable for the AV bridge modules.";
      }
      {
        assertion = !config.systemd.services.steamlink-framebuffer-console.enable;
        message = "HDMI audio hosts the single vendor PE itself; disable systemd.services.steamlink-framebuffer-console.";
      }
    ];

    users.groups.steamlink-pcm = { };
    # The FIFO outlives sink restarts: a writer then blocks in open() instead
    # of failing. Without the sink it cannot create a regular file here.
    systemd.tmpfiles.settings."10-steamlink-pcm" = {
      ${pcmDir}.d = {
        mode = "0750";
        user = "root";
        group = "steamlink-pcm";
      };
      ${fifo}.p = {
        mode = "0620";
        user = "root";
        group = "steamlink-pcm";
      };
    };

    systemd.services.steamlink-audio-pe = {
      description = "Headless vendor PE for Steam Link HDMI audio";
      wantedBy = [ "multi-user.target" ];
      after = [
        "local-fs.target"
        "systemd-udev-settle.service"
        "systemd-tmpfiles-setup.service"
      ]
      ++ lib.optional native "steamlink-writable-root-check.service";
      conflicts = [ "steamlink-framebuffer-console.service" ];
      unitConfig = {
        ConditionKernelCommandLine = "steamlink.console";
        ConditionPathExists =
          if native then
            "/opt/steamlink-vendor.sha256"
          else
            "/run/steamlink/lower/opt/steamlink-vendor.sha256";
      };
      # A configuration change must not restart PE: warm vendor
      # re-initialization is unqualified. Changes apply at the next boot.
      restartIfChanged = false;
      path = [
        pkgs.coreutils
        pkgs.util-linux
        pkgs.kmod
        pkgs.systemd
        pkgs.gawk
      ];
      environment = {
        STEAMLINK_PCM_DIR = pcmDir;
      }
      // lib.optionalAttrs native {
        STEAMLINK_ROOT_MODE = "ubifs-reclaimed";
        STEAMLINK_VENDOR_ROOT = "/opt/steamlink-vendor";
        STEAMLINK_VENDOR_SUMS = "/opt/steamlink-vendor.sha256";
        STEAMLINK_RECLAIM_CHECK = toString board.rootCheck;
      }
      // lib.optionalAttrs (board.media.hdmiMode != null) {
        PE_USER_DEFAULT_RESOLUTION = board.media.hdmiMode;
      };
      serviceConfig = {
        Type = "notify";
        NotifyAccess = "all";
        ExecStart = "${pkgs.runtimeShell} ${../av/audio/pe-host.sh}";
        TimeoutStartSec = 360;
        TimeoutStopSec = 15;
        KillMode = "control-group";
        OOMScoreAdjust = -500;
      };
    };
    systemd.services.steamlink-pcm-sink = {
      description = "Play the PCM FIFO through the vendor HDMI audio path";
      wantedBy = [ "multi-user.target" ];
      after = [ "steamlink-audio-pe.service" ];
      requires = [ "steamlink-audio-pe.service" ];
      bindsTo = [ "steamlink-audio-pe.service" ];
      # SLAudio polls SDL/ALSA devices every few seconds even on the HDMI
      # route; an empty ALSA configuration keeps libasound from logging a
      # missing alsa.conf each time.
      preStart = ''
        install -m 755 ${pcmSink}/bin/pcm-sink ${root}/run/pcm-sink
        install -m 644 /dev/null ${root}/run/alsa.conf
      '';
      serviceConfig = {
        Type = "exec";
        ExecStart = vendorRun "/run/pcm-sink -o ${toString audio.rate} 44100 /run/pcm/${baseNameOf fifo}";
        Restart = "on-failure";
        RestartSec = 3;
        TimeoutStopSec = 10;
        Nice = -10;
        LimitCORE = 0;
      };
    };
  };
}
