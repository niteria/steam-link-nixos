{
  trial,
  kernel,
  buildPackages,
}:
let
  check =
    buildPackages.runCommand "steamlink-state-core-tests"
      {
        nativeBuildInputs = [ buildPackages.stdenv.cc ];
      }
      ''
        cp ${./core.h} core.h
        cp ${./state-policy.h} state-policy.h
        cc -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined \
          -I${../../nand/writetest} -I${../../nand/hardened} -I. ${./test-core.c} -o test
        ./test
        cp ${./module.h} module.h
        cp ${./core.h} state-core.h
        cp ${./test-physical-stub.h} steamlink_state_physical.h
        cc -std=gnu11 -O1 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function \
          -fsanitize=address,undefined -I${../../nand/writetest} -I. ${./test-module.c} -o test-module
        ./test-module
        touch "$out"
      '';
in
trial.overrideAttrs (old: {
  pname = "steamlink-internal-state-experimental";
  prePatch = old.prePatch + ''
    mv steamlink_slc_trial.h steamlink_state_physical.h
    cp --remove-destination ${./state-policy.h} trial-policy.h
    cp ${./state-policy.h} state-policy.h
    cp ${./core.h} state-core.h
    cp ${./module.h} steamlink_slc_trial.h
  '';
  postPatch = old.postPatch + ''
    substituteInPlace Makefile --replace-fail steamlink_marvell_slc_trial steamlink_marvell_state
    substituteInPlace drivers/mtd/nand/raw/marvell_nand.c \
      --replace-fail '"steamlink-marvell-slc-trial"' '"steamlink-marvell-internal-state"'
  '';
  preBuild = old.preBuild + ''
    test -e ${check}
  '';
  installPhase = ''
    mkdir -p "$out/lib/modules/${kernel.modDirVersion}/extra"
    cp steamlink_marvell_state.ko steamlink_nand_overlay.ko "$out/lib/modules/${kernel.modDirVersion}/extra/"
    cp nand.dtbo "$out/"
  '';
  passthru.tests.stateCore = check;
  meta = old.meta // {
    description = "Experimental root-only UBI state writer with raw cursor reconstruction, tagged writes and fail-stop readback; never autoload";
  };
})
