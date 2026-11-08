{ stdenv, kernel }:
stdenv.mkDerivation {
  pname = "steamlink-reclaimed-pool";
  version = "0.1";
  dontUnpack = true;
  nativeBuildInputs = kernel.moduleBuildDependencies;
  hardeningDisable = [
    "pic"
    "format"
  ];
  buildPhase = ''
    cp ${./pool.c} pool.c
    cp ${./layout.h} layout.h
    cp ${../../nand/writetest/guard.h} guard.h
    printf '%s\n' 'obj-m += steamlink_reclaim_pool.o' 'steamlink_reclaim_pool-y := pool.o' > Makefile
    make -C ${kernel.dev}/lib/modules/${kernel.modDirVersion}/build M=$PWD ARCH=arm CROSS_COMPILE=${stdenv.cc.targetPrefix} modules
  '';
  installPhase = ''
    mkdir -p "$out/lib/modules/${kernel.modDirVersion}/extra"
    cp steamlink_reclaim_pool.ko "$out/lib/modules/${kernel.modDirVersion}/extra/"
  '';
}
