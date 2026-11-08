{
  lib,
  pkgs,
  steamlink-reference,
  ubiFastmap ? true,
  appendedDtb ? true,
  framebufferConsole ? false,
  etnaviv ? false,
  highResTimers ? true,
}:
let
  defconfigPatch = pkgs.buildPackages.runCommand "steamlink-defconfig.patch" { } ''
    mkdir -p a/arch/arm/configs b/arch/arm/configs
    cp ${steamlink-reference}/nixos/steamlink_defconfig b/arch/arm/configs/steamlink_defconfig
    diff -Naur a b > "$out" || test $? -eq 1
  '';
  kernel = pkgs.linuxKernel.kernels.linux_7_2.override {
    defconfig = "steamlink_defconfig";
    autoModules = false;
    preferBuiltin = false;
    enableCommonConfig = false;
    kernelPatches = [
      {
        name = "steamlink-defconfig";
        patch = defconfigPatch;
      }
    ];
    structuredExtraConfig =
      (with lib.kernel; {
        CMDLINE_FROM_BOOTLOADER = yes;
        CMDLINE = freeform "console=ttyS0,115200n8 rootwait rw init=/sbin/init";

        # Root filesystem, USB host/storage, and Ethernet are built into the board
        # defconfig. Keep modern userspace and the later network-hop workload usable.
        DEVTMPFS_MOUNT = yes;
        FANOTIFY = yes;
        SWAP = yes;
        ZRAM = module;
        ZRAM_BACKEND_ZSTD = yes;
        ZRAM_DEF_COMP_ZSTD = yes;
        MEMCG = yes;
        NAMESPACES = yes;
        UTS_NS = yes;
        IPC_NS = yes;
        USER_NS = yes;
        PID_NS = yes;
        NET_NS = yes;
        CGROUP_DEVICE = yes;
        CGROUP_FREEZER = yes;
        CGROUP_PIDS = yes;
        BPF_SYSCALL = yes;
        BPF_JIT = yes;
        CGROUP_BPF = yes;
        SECCOMP = yes;
        SECCOMP_FILTER = yes;
        TUN = yes;

        # The board DT already describes a GPIO reset line; the inherited config
        # omitted its controller/handler, leaving no platform restart function.
        GPIO_DWAPB = yes;
        POWER_RESET = yes;
        POWER_RESET_GPIO_RESTART = yes;

        NETFILTER_ADVANCED = yes;
        NF_TABLES = module;
        NF_TABLES_INET = yes;
        NF_TABLES_IPV4 = yes;
        NF_TABLES_IPV6 = yes;
        NFT_COMPAT = module;
        NFT_CT = module;
        NFT_LOG = module;
        NFT_LIMIT = module;
        NFT_REJECT = module;
        NFT_MASQ = module;
        NFT_NAT = module;
        NFT_FIB_INET = module;
        NFT_FIB_IPV4 = module;
        NFT_FIB_IPV6 = module;
        IP6_NF_IPTABLES = module;
      })
      // lib.optionalAttrs (highResTimers != null) {
        # null preserves the installed bootstrap's defconfig. Media trials use
        # high-resolution clockevents rather than rounding 1ms waits to100Hz ticks.
        HIGH_RES_TIMERS = if highResTimers then lib.kernel.yes else lib.kernel.no;
      }
      // lib.optionalAttrs (!ubiFastmap) { MTD_UBI_FASTMAP = lib.kernel.no; }
      // lib.optionalAttrs (!appendedDtb) { ARM_APPENDED_DTB = lib.kernel.no; }
      // lib.optionalAttrs framebufferConsole (
        with lib.kernel;
        {
          VT = yes;
          VT_CONSOLE = yes;
          VT_HW_CONSOLE_BINDING = yes;
          DUMMY_CONSOLE = yes;
          FB = yes;
          FB_VIRTUAL = module;
          FRAMEBUFFER_CONSOLE = yes;
          FRAMEBUFFER_CONSOLE_DEFERRED_TAKEOVER = no;
          FONT_8x8 = yes;
          FONT_8x16 = yes;
          INPUT_EVDEV = yes;
          HID_GENERIC = yes;
          USB_HID = yes;
        }
      )
      // lib.optionalAttrs etnaviv (
        with lib.kernel;
        {
          DRM = module;
          DRM_ETNAVIV = module;
          DRM_ETNAVIV_THERMAL = yes;
          CMA = yes;
          DMA_CMA = yes;
          CMA_SIZE_MBYTES = freeform "16";
        }
      );
  };
in
kernel
