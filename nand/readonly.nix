{
  lib,
  stdenv,
  buildPackages,
  kernel,
  kernelModuleMakeFlags,
  withBbt ? false,
}:
let
  opcodeCheck =
    buildPackages.runCommand "steamlink-nand-readonly-opcodes"
      {
        nativeBuildInputs = [ buildPackages.stdenv.cc ];
      }
      ''
        cc -Wall -Wextra -Werror -I${./readonly} ${./readonly/opcodes-test.c} -o test
        ./test
        touch "$out"
      '';
  bbtCheck =
    buildPackages.runCommand "steamlink-nand-readonly-bbt-policy"
      {
        nativeBuildInputs = [ buildPackages.stdenv.cc ];
      }
      ''
        cc -O2 -Wall -Wextra -Werror -fsanitize=address,undefined \
          -I${./bbt} ${./bbt/policy-test.c} -o test
        ./test
        touch "$out"
      '';
in
stdenv.mkDerivation {
  pname = "steamlink-nand-readonly" + lib.optionalString withBbt "-bbt";
  version = "0.1";
  src = ./readonly;
  patches = [
    ./readonly-driver.patch
    ./ecc-error-contract.patch
  ]
  ++ lib.optional withBbt ./bbt-readonly.patch;
  nativeBuildInputs = kernel.moduleBuildDependencies ++ [
    buildPackages.dtc
    buildPackages.xxd
  ];
  hardeningDisable = [ "pic" ];
  prePatch = ''
    mkdir -p drivers/mtd/nand/raw
    tar -xOf ${kernel.src} linux-${kernel.version}/drivers/mtd/nand/raw/marvell_nand.c > drivers/mtd/nand/raw/marvell_nand.c
  ''
  + lib.optionalString withBbt ''
    cp ${./bbt/policy.h} policy.h
    cp ${./bbt/steamlink_bbt_readonly.h} steamlink_bbt_readonly.h
  '';
  preBuild = ''
    test -e ${opcodeCheck}
    ${lib.optionalString withBbt "test -e ${bbtCheck}"}
    dtc -@ -I dts -O dtb -o nand.dtbo nand-overlay.dts
    # Parent cell widths are compile-time context, not changes to the live SoC
    # node. Removing them avoids reconfiguring unrelated IRQ-provider links.
    fdtput -d nand.dtbo /fragment@0/__overlay__ '#address-cells'
    fdtput -d nand.dtbo /fragment@0/__overlay__ '#size-cells'
    fdtoverlay -i ${kernel}/dtbs/berlin2cd-valve-steamlink.dtb -o merged.dtb nand.dtbo
    test "$(fdtget -t x merged.dtb /soc@f7000000/nand-controller@f00000 reg)" = 'f00000 100'
    xxd -i nand.dtbo > nand-overlay-data.h
  '';
  makeFlags = kernelModuleMakeFlags ++ [
    "KDIR=${kernel.dev}/lib/modules/${kernel.modDirVersion}/build"
  ];
  installPhase = ''
    mkdir -p "$out/lib/modules/${kernel.modDirVersion}/extra"
    cp steamlink_marvell_nand.ko steamlink_nand_overlay.ko "$out/lib/modules/${kernel.modDirVersion}/extra/"
    cp nand.dtbo "$out/"
  '';
  passthru.tests.opcodes = opcodeCheck;
  passthru.tests.bbtPolicy = bbtCheck;
  meta = {
    description = "Experimental read-only Berlin NAND support using the modern Marvell driver";
    license = lib.licenses.gpl2Only;
    platforms = [ "armv7l-linux" ];
  };
}
