{ stdenv, kernel, kernelModuleMakeFlags }:
stdenv.mkDerivation {
  pname = "steamlink-av-shmcc-foundations";
  version = "0.1";
  src = builtins.path {
    path = ./.;
    name = "steamlink-av-shmcc-source";
    filter = path: type: type == "directory" || builtins.elem (baseNameOf path) [ "module.c" "core.h" "uapi.h" "layout.h" "Makefile" ];
  };
  nativeBuildInputs = kernel.moduleBuildDependencies;
  hardeningDisable = [ "pic" ];
  makeFlags = kernelModuleMakeFlags ++ [ "KDIR=${kernel.dev}/lib/modules/${kernel.modDirVersion}/build" ];
  installPhase = ''
    install -Dm644 steamlink_av_shmcc.ko "$out/lib/modules/${kernel.modDirVersion}/extra/steamlink_av_shmcc.ko"
    cp Module.symvers "$out/"
  '';
}
