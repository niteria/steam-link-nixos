{ stdenv, kernel, kernelModuleMakeFlags, buildPackages, sdk, shmcc }:
stdenv.mkDerivation {
  pname = "steamlink-pe-modern";
  version = "0.1";
  # The qualified build snapshot was this directory; generated Python caches
  # are excluded and do not take part in the build.
  src = builtins.path {
    path = ./.;
    name = "pe";
    filter = path: type: baseNameOf path != "__pycache__";
  };
  nativeBuildInputs = kernel.moduleBuildDependencies ++ [ buildPackages.python3 ];
  hardeningDisable = [ "pic" ];
  postPatch = ''
    cp ${../compat/layout.h} av-memory-layout.h
    cp -r ${sdk}/kernel/arch/arm/mach-berlin sdk
    chmod -R u+w sdk
    cp sdk/modules/pe/{pe_driver.c,avio_dhub_drv.c} ./
    cp sdk/modules/nfc/hal_dhub.c ./
    python3 adapt.py
  '';
  makeFlags = kernelModuleMakeFlags ++ [ "KDIR=${kernel.dev}/lib/modules/${kernel.modDirVersion}/build" "KBUILD_EXTRA_SYMBOLS=${shmcc}/Module.symvers" ];
  installPhase = ''install -Dm644 steamlink_pe.ko "$out/lib/modules/${kernel.modDirVersion}/extra/steamlink_pe.ko"'';
}
