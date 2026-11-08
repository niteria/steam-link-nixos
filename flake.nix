{
  description = "Reusable NixOS support for Valve Steam Link 1003";

  inputs = {
    # This set builds the qualified kernel and early NAND userspace. Consumers
    # deliberately do not make it follow their desktop's nixpkgs input.
    platform-nixpkgs.url = "github:NixOS/nixpkgs/44a91898084f46797b5fac650c7e8c9ac38c43d4";
    steamlink-reference = {
      url = "github:Ondra-Zik/steamlink-nixos/9734b750784c48b1d67b580b7086bd6d5eaa26f8";
      flake = false;
    };
  };

  outputs =
    {
      self,
      platform-nixpkgs,
      steamlink-reference,
    }:
    let
      nixpkgs = platform-nixpkgs;
    in
    {
      nixosModules = {
        default = {
          # Order matches the installed system's package and module list order.
          imports = [
            ./modules/relay.nix
            ./modules/board.nix
            ./modules/media.nix
            ./nixos/wifi.nix
            ./nixos/radio-identity.nix
            ./modules/hdmi-audio.nix
          ];
          _module.args.steamlink-reference = steamlink-reference;
          # Build-side helpers (presenter tests, diagnostics assets, public SDK
          # fetches) use the pinned native set, as in the qualified builds.
          _module.args.steamlinkBuildPkgs = nixpkgs.legacyPackages.x86_64-linux;
          # For consumers building against the vendor display and audio ABI.
          _module.args.steamlinkStockRuntime = import ./av/stock-runtime.nix {
            pkgs = nixpkgs.legacyPackages.x86_64-linux;
          };
          _module.args.steamlinkConsoleSource = ./console;
        };
        steamlink = self.nixosModules.default;
        tailscaleRouter = ./modules/relay.nix;
      };

      lib.mkSystem =
        {
          modules,
          specialArgs ? { },
        }:
        nixpkgs.lib.nixosSystem {
          inherit specialArgs;
          modules = [ self.nixosModules.default ] ++ modules;
        };

      # A credential-free example is also the package/check instantiation. It
      # selects read-only USB operation, never an enrolled writable NAND root.
      nixosConfigurations.example = self.lib.mkSystem {
        modules = [ ./examples/read-only.nix ];
      };

      packages.x86_64-linux =
        let
          platform = self.nixosConfigurations.example.config.hardware.steamlink.packages;
        in
        {
          inherit (platform)
            kernel
            readonly
            reclaimed
            pool
            ;
          default = platform.kernel;
        };

      checks.x86_64-linux =
        let
          platform = self.nixosConfigurations.example.config.hardware.steamlink.packages;
        in
        platform.checks
        // {
          module-contract = import ./tests/module-contract.nix { inherit self nixpkgs; };
          qualified-artifacts =
            nixpkgs.legacyPackages.x86_64-linux.runCommand "steamlink-qualified-artifacts" { }
              ''
                mkdir -p "$out"
                printf '%s  %s\n' \
                  2842359b388ec38bebd89a2f78a71171ceea8438b7f9841175dbd69c1536b82b ${platform.kernel}/zImage \
                  73b224f7934a49f570fe6d912871a45c5a725e7c611940b87ee8d828dda591b7 ${platform.reclaimed}/lib/modules/7.2.6/extra/steamlink_marvell_reclaim.ko \
                  3df9e183d16d84818a5f77c2e3d07eb460f9f6b164093306404574b4591d3aa4 ${platform.pool}/lib/modules/7.2.6/extra/steamlink_reclaim_pool.ko \
                  | sha256sum -c - > "$out/result.txt"
              '';
        };

      formatter.x86_64-linux = nixpkgs.legacyPackages.x86_64-linux.nixfmt;
    };
}
