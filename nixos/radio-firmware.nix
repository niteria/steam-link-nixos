{ pkgs, steamlink-reference }:
pkgs.runCommand "steamlink-radio-firmware" { } ''
  mkdir -p "$out/lib/firmware/mrvl"
  cp ${steamlink-reference}/vendor/wireless/firmware/mrvl/sd8897_uapsta.bin "$out/lib/firmware/mrvl/"
  cp ${steamlink-reference}/vendor/wireless/firmware/regulatory.db* "$out/lib/firmware/"
''
