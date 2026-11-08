{
  readonly,
  kernel,
  buildPackages,
}:
let
  check =
    buildPackages.runCommand "steamlink-slc-trial-bounds"
      {
        nativeBuildInputs = [ buildPackages.stdenv.cc ];
      }
      ''
        cc -O2 -Wall -Wextra -Werror -fsanitize=address,undefined \
          -I${./slc-trial} -I${./writetest} ${./slc-trial/policy-test.c} -o test
        ./test
        touch "$out"
      '';
in
readonly.overrideAttrs (old: {
  pname = "steamlink-slc-trial";
  patches = old.patches ++ [ ./slc-trial.patch ];
  prePatch = old.prePatch + ''
    cp ${./slc-trial/policy.h} trial-policy.h
    cp ${./writetest/guard.h} guard.h
    cp ${./slc-trial/steamlink_slc_trial.h} steamlink_slc_trial.h
  '';
  postPatch = ''
    substituteInPlace Makefile --replace-fail steamlink_marvell_nand steamlink_marvell_slc_trial
    substituteInPlace drivers/mtd/nand/raw/marvell_nand.c \
      --replace-fail '"steamlink-marvell-nfc-ro"' '"steamlink-marvell-slc-trial"'
  '';
  preBuild = old.preBuild + ''
    test -e ${check}
  '';
  installPhase = ''
    mkdir -p "$out/lib/modules/${kernel.modDirVersion}/extra"
    cp steamlink_marvell_slc_trial.ko steamlink_nand_overlay.ko "$out/lib/modules/${kernel.modDirVersion}/extra/"
    cp nand.dtbo "$out/"
  '';
  passthru.tests.bounds = check;
  meta = old.meta // {
    description = "Bounded 32 MiB candidate-SLC filesystem trial; never autoload";
  };
})
