{ stdenv, kernel, kernelModuleMakeFlags, shmcc }:
stdenv.mkDerivation {
  pname = "steamlink-pe-agent-vm";
  version = "0.1";
  src = ./.;
  nativeBuildInputs = kernel.moduleBuildDependencies;
  hardeningDisable = [ "pic" ];
  postPatch = ''
    printf '%s\n' 'obj-m += steamlink_agent_vm.o' 'steamlink_agent_vm-y := agent.o agent-vm.o' > Makefile
  '';
  buildPhase = ''make -C ${kernel.dev}/lib/modules/${kernel.modDirVersion}/build M="$PWD" modules ${builtins.concatStringsSep " " kernelModuleMakeFlags} KBUILD_EXTRA_SYMBOLS=${shmcc}/Module.symvers'';
  installPhase = ''install -Dm644 steamlink_agent_vm.ko "$out/lib/modules/${kernel.modDirVersion}/extra/steamlink_agent_vm.ko"'';
}
