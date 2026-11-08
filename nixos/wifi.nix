{
  config,
  lib,
  pkgs,
  steamlink-reference,
  ...
}:
let
  cfg = config.services.steamlinkWifi;
in
{
  options.services.steamlinkWifi = {
    enable = lib.mkEnableOption "Steam Link Wi-Fi using a separately provisioned WPA configuration";
    configurationFile = lib.mkOption {
      type = lib.types.str;
      default = "/var/lib/steamlink-wifi/wpa.conf";
      description = "Root-only WPA configuration provisioned outside the Nix store";
    };
  };
  config = lib.mkIf cfg.enable {
    hardware.firmware = [ (import ./radio-firmware.nix { inherit pkgs steamlink-reference; }) ];
    # The board's minimal defconfig lacks FW_LOADER_COMPRESS_ZSTD. NixOS's
    # version-based default would leave only .zst files the driver cannot load.
    hardware.firmwareCompression = "none";
    environment.systemPackages = [
      pkgs.iw
      pkgs.wpa_supplicant
    ];
    boot.kernel.sysctl = {
      "net.ipv4.conf.all.arp_ignore" = 1;
      "net.ipv4.conf.all.arp_announce" = 2;
    };
    systemd.network.networks."20-wifi" = {
      matchConfig.Name = "mlan0";
      networkConfig = {
        DHCP = "ipv4";
        IPv6AcceptRA = false;
        DNSDefaultRoute = true;
      };
      dhcpV4Config = {
        RouteMetric = 2048;
        UseDNS = true;
        SendHostname = false;
      };
    };
    systemd.services.steamlink-wifi = {
      description = "Steam Link Wi-Fi association";
      wantedBy = [ "multi-user.target" ];
      after = [ "sys-subsystem-net-devices-mlan0.device" ];
      bindsTo = [ "sys-subsystem-net-devices-mlan0.device" ];
      unitConfig.ConditionPathExists = cfg.configurationFile;
      serviceConfig = {
        Type = "exec";
        ExecStartPre = [
          "${pkgs.iproute2}/bin/ip link set dev mlan0 up"
          "${pkgs.iw}/bin/iw dev mlan0 set power_save off"
        ];
        ExecStart = "${pkgs.wpa_supplicant}/bin/wpa_supplicant -Dnl80211 -imlan0 -c${cfg.configurationFile}";
        Restart = "on-failure";
        RestartSec = 5;
        RuntimeDirectory = [
          "steamlink-wifi"
          "wpa_supplicant"
        ];
        RuntimeDirectoryMode = "0700";
      };
    };
  };
}
