{ stdenv, kernel, kernelModuleMakeFlags, buildPackages, busFrequency ? 400000 }:
assert builtins.elem busFrequency [ 100000 400000 ];
stdenv.mkDerivation {
  pname = "steamlink-twsi-modern";
  version = "0.1";
  src = ./twsi.c;
  dontUnpack = true;
  nativeBuildInputs = kernel.moduleBuildDependencies ++ [ buildPackages.dtc buildPackages.xxd ];
  hardeningDisable = [ "pic" ];
  buildPhase = ''
    cp "$src" steamlink_twsi.c
    cp ${./twsi-overlay.dts} twsi-overlay.dts
    substituteInPlace twsi-overlay.dts --replace-fail '<400000>' '<${toString busFrequency}>'
    dtc -@ -I dts -O dtb twsi-overlay.dts -o twsi.dtbo
    xxd -i -n twsi_overlay twsi.dtbo > twsi-overlay-data.h
    # libfdt requires an 8-byte-aligned DTB pointer. A plain unsigned-char array
    # can land at data+4 depending on module layout (notably alongside UBI).
    substituteInPlace twsi-overlay-data.h --replace-fail \
      'unsigned char twsi_overlay[]' 'unsigned char twsi_overlay[] __attribute__((aligned(8)))'
    printf '%s\n' 'obj-m += steamlink_twsi.o' > Makefile
    make -C ${kernel.dev}/lib/modules/${kernel.modDirVersion}/build M="$PWD" modules ${builtins.concatStringsSep " " kernelModuleMakeFlags}
  '';
  installPhase = ''install -Dm644 steamlink_twsi.ko "$out/lib/modules/${kernel.modDirVersion}/extra/steamlink_twsi.ko"'';
}
