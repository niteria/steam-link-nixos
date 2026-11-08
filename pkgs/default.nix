{
  pkgs,
  lib,
  steamlink-reference,
}:
let
  host = pkgs.buildPackages;
  kernel = import ../nixos/kernel.nix {
    inherit pkgs lib steamlink-reference;
    ubiFastmap = false;
    appendedDtb = false;
    framebufferConsole = true;
    etnaviv = true;
    highResTimers = true;
  };
  k = pkgs.linuxPackagesFor kernel;
  readonly = k.callPackage ../nand/readonly.nix { withBbt = true; };
  hardened = k.callPackage ../nand/hardened-read.nix { inherit readonly; };
  trial = k.callPackage ../nand/slc-trial.nix { readonly = hardened; };
  state = k.callPackage ../internal/state/module.nix { inherit trial; };
  layoutCheck =
    host.runCommand "steamlink-reclaim-layout-tests"
      {
        nativeBuildInputs = [
          host.stdenv.cc
          host.python3
        ];
      }
      ''
        cp ${../internal/state/core.h} state-core.h
        cp ${../internal/reclaim/state-policy.h} state-policy.h
        cp ${../internal/reclaim/layout.h} layout.h
        cp ${../nand/writetest/guard.h} guard.h
        python3 ${../internal/reclaim/prepare-source.py}
        cc -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined -I. ${../internal/reclaim/test-layout.c} -o test
        mkdir -p "$out"
        ./test > "$out/result.txt"
        # Write-fault classification under the deployed reclaim aperture.
        cp ${../internal/state/module.h} module.h
        cp ${../internal/state/test-physical-stub.h} steamlink_state_physical.h
        cc -std=gnu11 -O1 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function \
          -fsanitize=address,undefined -I. ${../internal/state/test-module.c} -o test-module
        ./test-module >> "$out/result.txt"
      '';
  reclaimed = k.callPackage ../internal/reclaim/module.nix {
    stateNand = state;
    checks = layoutCheck;
  };
  pool = k.callPackage ../internal/reclaim/pool.nix { };
in
{
  inherit
    kernel
    readonly
    reclaimed
    pool
    ;
  checks = {
    nand-opcodes = readonly.tests.opcodes;
    nand-bbt = readonly.tests.bbtPolicy;
    nand-read-recovery = hardened.tests.readRecovery;
    nand-slc-bounds = trial.tests.bounds;
    nand-state-core = state.tests.stateCore;
    nand-reclaim-layout = layoutCheck;
  };
}
