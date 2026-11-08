{ pkgs }:
pkgs.fetchgit {
  url = "https://github.com/ValveSoftware/steamlink-sdk.git";
  rev = "62b4d098d1472c3534dd098ca2a0e0e10712f1c6";
  sparseCheckout = [ "kernel/arch/arm/mach-berlin" ];
  hash = "sha256-9AQxA+Cvb4a+vQe8vHVb1OrUwu3Trc54lH/eElLNKVY=";
}
