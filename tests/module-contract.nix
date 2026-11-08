{ self, nixpkgs }:
let
  lib = nixpkgs.lib;
  host = nixpkgs.legacyPackages.x86_64-linux;
  fixture = {
    networking.hostName = "fixture-router";
    hardware.steamlink = {
      ethernetAddress = "02:00:00:00:00:02";
      storage = {
        mode = "reclaimed";
        snapshot = lib.concatStrings (lib.replicate 64 "b");
        stateNonce = "ffffffffffffffff";
        seedImageHash = lib.concatStrings (lib.replicate 64 "c");
        expectedBadBlocks = 3;
        recoveryUsbSerial = "SYNTHETIC_DRIVE";
      };
    };
    services.steamlinkRelay = {
      enable = true;
      subnets = [
        "10.44.0.0/24"
        "172.20.1.0/24"
      ];
      lanInterfaces = [ "eth0" ];
    };
    system.stateVersion = "26.05";
  };
  evaluated = self.lib.mkSystem { modules = [ fixture ]; };
  cfg = evaluated.config;
  standaloneRouter = nixpkgs.lib.nixosSystem {
    system = "x86_64-linux";
    modules = [
      self.nixosModules.tailscaleRouter
      {
        inherit (fixture) networking services;
      }
    ];
  };
  withoutNonce = self.lib.mkSystem {
    modules = [
      fixture
      { hardware.steamlink.storage.stateNonce = lib.mkForce null; }
    ];
  };
  invalidSubnet = self.lib.mkSystem {
    modules = [
      fixture
      { services.steamlinkRelay.subnets = lib.mkForce [ "999.1.1.0/24" ]; }
    ];
  };
  audioFixture = {
    hardware.steamlink.media.enable = true;
    hardware.steamlink.hdmiAudio.enable = true;
    systemd.services.steamlink-framebuffer-console.enable = false;
  };
  audioWithConsole = self.lib.mkSystem {
    modules = [
      fixture
      audioFixture
      { systemd.services.steamlink-framebuffer-console.enable = lib.mkForce true; }
    ];
  };
  hdmiCfg =
    (self.lib.mkSystem {
      modules = [
        fixture
        audioFixture
      ];
    }).config;
  rootCheck = lib.removePrefix "exec " cfg.systemd.services.steamlink-writable-root-check.script;
  valid =
    assert builtins.all (item: item.assertion) cfg.assertions;
    assert standaloneRouter.config.networking.nftables.enable;
    assert standaloneRouter.config.networking.firewall.enable;
    assert cfg.nixpkgs.hostPlatform.system == "armv7l-linux";
    assert cfg.fileSystems."/".fsType == "ubifs";
    assert !cfg.systemd.shutdownRamfs.enable;
    assert builtins.elem "--hostname=fixture-router" cfg.services.tailscale.extraSetFlags;
    assert builtins.elem "--advertise-routes=10.44.0.0/24,172.20.1.0/24"
      cfg.services.tailscale.extraSetFlags;
    assert lib.hasInfix "10.44.0.0/24, 172.20.1.0/24"
      cfg.networking.nftables.tables.steamlink-tailnet-nat.content;
    assert cfg.users.users.root.openssh.authorizedKeys.keys == [ ];
    assert !(builtins.all (item: item.assertion) withoutNonce.config.assertions);
    assert !(cfg.systemd.services ? steamlink-audio-pe);
    assert builtins.all (item: item.assertion) hdmiCfg.assertions;
    assert !(builtins.all (item: item.assertion) audioWithConsole.config.assertions);
    assert !hdmiCfg.systemd.services.steamlink-audio-pe.restartIfChanged;
    assert hdmiCfg.users.groups ? steamlink-pcm;
    assert
      !(builtins.tryEval (builtins.deepSeq invalidSubnet.config.services.steamlinkRelay.subnets true))
      .success;
    true;
in
assert valid;
host.runCommand "steamlink-module-contract-tests"
  {
    nativeBuildInputs = [
      host.python3
      host.bash
    ];
  }
  ''
    mkdir -p "$out"
    mkdir -p source/internal/writable source/internal/reclaim
    cp ${../internal/writable/init-transform.py} source/internal/writable/init-transform.py
    cp ${../internal/reclaim/init-transform.py} source/internal/reclaim/init-transform.py
    python3 ${./test-bootstrap.py} source -v > "$out/bootstrap.txt" 2>&1
    test "$(bash -c "printf '%u' 0xffffffffffffffff")" = 18446744073709551615
    cp ${rootCheck} "$out/root-check.sh"
    bash -n "$out/root-check.sh"
    grep -q '0xffffffffffffffff' "$out/root-check.sh"
    grep -q 'bad_peb_count)" = 3' "$out/root-check.sh"
    printf '%s\n' 'PASS: synthetic identities, root admission, caller-owned policy and guarded shutdown' > "$out/result.txt"
  ''
