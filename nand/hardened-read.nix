{
  readonly,
  kernel,
  buildPackages,
  lib,
  faultTest ? false,
}:
let
  moduleName = "steamlink_marvell_hardened" + lib.optionalString faultTest "_fault";
  check =
    buildPackages.runCommand "steamlink-nand-hardened-read-tests"
      {
        nativeBuildInputs = [ buildPackages.stdenv.cc ];
      }
      ''
        cc -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined \
          -I${./hardened} ${./hardened/read-test.c} -o test
        ./test
        touch "$out"
      '';
in
readonly.overrideAttrs (old: {
  pname = "steamlink-nand-hardened-read" + lib.optionalString faultTest "-fault";
  patches =
    old.patches ++ [ ./hardened-read.patch ] ++ lib.optional faultTest ./hardened-ecc-fault.patch;
  prePatch = old.prePatch + ''
    cp ${./hardened/erased.h} erased.h
    cp ${./hardened/transport.h} transport.h
    cp ${./hardened/steamlink_hardened_read.h} steamlink_hardened_read.h
  '';
  preBuild = old.preBuild + ''
    test -e ${check}
  '';
  postPatch = ''
    substituteInPlace Makefile --replace-fail steamlink_marvell_nand ${moduleName}
    substituteInPlace drivers/mtd/nand/raw/marvell_nand.c \
      --replace-fail '"steamlink-marvell-nfc-ro"' '"steamlink-marvell-hardened${lib.optionalString faultTest "-fault"}-ro"'
  '';
  installPhase = ''
    mkdir -p "$out/lib/modules/${kernel.modDirVersion}/extra"
    cp ${moduleName}.ko steamlink_nand_overlay.ko "$out/lib/modules/${kernel.modDirVersion}/extra/"
    cp nand.dtbo "$out/"
  '';
  passthru.tests.readRecovery = check;
  meta = old.meta // {
    description = "Experimental parity-aware Berlin read recovery; never autoload";
  };
})
