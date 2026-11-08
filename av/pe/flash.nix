{ stdenv, kernel, kernelModuleMakeFlags }:
stdenv.mkDerivation {
  pname = "steamlink-pe-readonly-bootloader";
  version = "0.1";
  src = ./flash.c;
  dontUnpack = true;
  nativeBuildInputs = kernel.moduleBuildDependencies;
  hardeningDisable = [ "pic" ];
  buildPhase = ''
    cp "$src" steamlink_pe_flash.c
    printf '%s\n' 'obj-m += steamlink_pe_flash.o' > Makefile
    make -C ${kernel.dev}/lib/modules/${kernel.modDirVersion}/build M="$PWD" modules ${builtins.concatStringsSep " " kernelModuleMakeFlags}
  '';
  installPhase = ''install -Dm644 steamlink_pe_flash.ko "$out/lib/modules/${kernel.modDirVersion}/extra/steamlink_pe_flash.ko"'';
}
