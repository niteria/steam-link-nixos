{ stdenv, kernel, kernelModuleMakeFlags }:
stdenv.mkDerivation {
  pname = "steamlink-gpio-modern";
  version = "0.1";
  src = ./gpio.c;
  dontUnpack = true;
  nativeBuildInputs = kernel.moduleBuildDependencies;
  hardeningDisable = [ "pic" ];
  buildPhase = ''
    cp "$src" steamlink_gpio.c
    printf '%s\n' 'obj-m += steamlink_gpio.o' > Makefile
    make -C ${kernel.dev}/lib/modules/${kernel.modDirVersion}/build M="$PWD" modules ${builtins.concatStringsSep " " kernelModuleMakeFlags}
  '';
  installPhase = ''install -Dm644 steamlink_gpio.ko "$out/lib/modules/${kernel.modDirVersion}/extra/steamlink_gpio.ko"'';
}
