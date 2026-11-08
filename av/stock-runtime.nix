{ pkgs }:
let
  base = "https://raw.githubusercontent.com/ValveSoftware/steamlink-sdk/62b4d098d1472c3534dd098ca2a0e0e10712f1c6/rootfs";
  files = {
    "usr/lib/crt1.o" = "sha256-t7g/UsECUDPMhqDugCa5QQyh+rbrA5t/j0ei1F6pjUA=";
    "usr/lib/crti.o" = "sha256-BocQP5iISy8y1G4WKur9w8kHF0UuBgFOw1gl0FA6SQg=";
    "usr/lib/crtn.o" = "sha256-R5xp+cC7KOyvZuuv4VOmeu9essOtuQHqi6hpZ2+GVJA=";
    "usr/lib/libc_nonshared.a" = "sha256-osIoFtjfpwyVMZOEs1k2b/3hTCOhDkrs6vAFAR2AZdw=";
    "lib/libc-2.19.so" = "sha256-UOLy9horg+JJWyM/ROYObESiibnlwQt5rSvPugPqyG0=";
    "lib/libdl-2.19.so" = "sha256-FIPv+7H40CbjYorRK/A6qPrf03orUYff5J2mZ1gzPsQ=";
    "lib/ld-2.19.so" = "sha256-W5X6aamyULin34iz4vJn7g9m1Q0bGBvjjp1WkFrzHHY=";
    "usr/include/SLVideo.h" = "sha256-8lIEP1bmWGVS1Zi3UUw7Kq2gLyPFaO+shClBYATobCs=";
    "usr/include/SLAudio.h" = "sha256-s19vJjinjuFgFXUEbL5J9oG/hwTOL6y7qiDPjB2HkzA=";
  };
in pkgs.runCommand "steamlink-stock-av-abi-runtime" { } (
  builtins.concatStringsSep "\n" (pkgs.lib.mapAttrsToList (name: hash: ''
    mkdir -p "$out/$(dirname ${name})"
    cp ${pkgs.fetchurl { url = "${base}/${name}"; inherit hash; }} "$out/${name}"
  '') files) + ''
    chmod +x "$out/lib/ld-2.19.so"
    ln -s libc-2.19.so "$out/lib/libc.so.6"
    ln -s libdl-2.19.so "$out/lib/libdl.so.2"
    ln -s ld-2.19.so "$out/lib/ld-linux-armhf.so.3"
  ''
)
