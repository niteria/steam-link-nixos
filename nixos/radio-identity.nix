{
  config,
  lib,
  pkgs,
  ...
}:
let
  cfg = config.services.steamlinkRadioIdentity;
  mac = lib.types.strMatching "([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}";
  addressScript = pkgs.writeShellScript "steamlink-bluetooth-factory-address" ''
    set -eu
    for n in $(${pkgs.coreutils}/bin/seq 1 30); do
      test ! -e /sys/class/bluetooth/hci0 || break
      ${pkgs.coreutils}/bin/sleep 1
    done
    test -e /sys/class/bluetooth/hci0
    driver=$(${pkgs.coreutils}/bin/readlink -f /sys/class/bluetooth/hci0/device/driver)
    test "$(${pkgs.coreutils}/bin/basename "$driver")" = btmrvl_sdio
    ${pkgs.bluez}/bin/btmgmt --index hci0 power off
    ${pkgs.bluez}/bin/btmgmt --index hci0 public-addr ${lib.escapeShellArg cfg.bluetoothAddress}
    matched=0
    for n in $(${pkgs.coreutils}/bin/seq 1 30); do
      if ${pkgs.bluez}/bin/btmgmt --index hci0 info | ${pkgs.gnugrep}/bin/grep -Fiq ${lib.escapeShellArg "addr ${cfg.bluetoothAddress} "}; then
        matched=1
        break
      fi
      ${pkgs.coreutils}/bin/sleep 1
    done
    test "$matched" = 1
    ${pkgs.bluez}/bin/btmgmt --index hci0 power off
  '';
in
{
  options.services.steamlinkRadioIdentity = {
    enable = lib.mkEnableOption "explicit factory radio identities recovered from this board's backup";
    wifiAddress = lib.mkOption {
      type = mac;
      description = "Factory WIFI_MAC_ADDR, not an inferred adjacent address";
    };
    bluetoothAddress = lib.mkOption {
      type = mac;
      description = "Stock cached-selection/device-ID-derived Bluetooth address";
    };
  };
  config = lib.mkIf cfg.enable {
    systemd.network.links."10-steamlink-wifi-identity" = {
      matchConfig = {
        OriginalName = "mlan0";
        Driver = "mwifiex_sdio";
      };
      linkConfig = {
        MACAddressPolicy = "none";
        MACAddress = cfg.wifiAddress;
      };
    };
    systemd.services.bluetooth = lib.mkIf config.hardware.bluetooth.enable {
      serviceConfig.ExecStartPre = [ addressScript ];
      serviceConfig.TimeoutStartSec = 75;
    };
  };
}
