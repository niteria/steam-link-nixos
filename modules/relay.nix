{
  config,
  lib,
  pkgs,
  ...
}:
let
  cfg = config.services.steamlinkRelay;
  validCidr =
    value:
    builtins.match "([0-9]{1,3}\\.){3}[0-9]{1,3}/[0-9]{1,2}" value != null
    && (
      let
        parts = lib.splitString "/" value;
        octets = lib.splitString "." (builtins.head parts);
        decimal = text: builtins.match "0|[1-9][0-9]*" text != null;
      in
      builtins.all decimal octets
      && decimal (builtins.elemAt parts 1)
      && builtins.all (part: lib.toInt part <= 255) octets
      && lib.toInt (builtins.elemAt parts 1) <= 32
    );
  subnetSet =
    if builtins.length cfg.subnets == 1 then
      builtins.head cfg.subnets
    else
      "{ " + lib.concatStringsSep ", " cfg.subnets + " }";
  interfaceSet = "{ " + lib.concatMapStringsSep ", " builtins.toJSON cfg.lanInterfaces + " }";
  tunnel = builtins.toJSON config.services.tailscale.interfaceName;
in
{
  options.services.steamlinkRelay = {
    enable = lib.mkEnableOption "headless Tailscale subnet routing compatible with the qualified Steam Link kernel";
    subnets = lib.mkOption {
      type = lib.types.listOf (lib.types.addCheck lib.types.str validCidr);
      default = [ ];
    };
    lanInterfaces = lib.mkOption {
      type = lib.types.listOf (lib.types.strMatching "[A-Za-z0-9_.:-]+");
      default = [ "eth0" ];
    };
    acceptRoutes = lib.mkOption {
      type = lib.types.bool;
      default = true;
    };
  };
  config = lib.mkIf cfg.enable {
    assertions = [
      {
        assertion = cfg.subnets != [ ] && cfg.lanInterfaces != [ ];
        message = "Steam Link relay requires explicit LAN subnets and interfaces.";
      }
      {
        assertion = config.services.tailscale.interfaceName != "userspace-networking";
        message = "Steam Link subnet routing requires the kernel TUN interface.";
      }
    ];
    services.tailscale = {
      enable = true;
      openFirewall = true;
      useRoutingFeatures = "both";
      disableTaildrop = lib.mkDefault true;
      extraSetFlags = [
        "--hostname=${config.networking.hostName}"
        "--accept-routes=${lib.boolToString cfg.acceptRoutes}"
        "--advertise-routes=${lib.concatStringsSep "," cfg.subnets}"
        "--netfilter-mode=off"
      ];
    };
    systemd.services.tailscaled.environment.TS_DEBUG_FIREWALL_MODE = "nftables";
    systemd.services.tailscaled-set.serviceConfig.ExecCondition =
      pkgs.writeShellScript "steamlink-tailscale-enrolled" ''
        state=$(${config.services.tailscale.package}/bin/tailscale status --json --peers=false | ${pkgs.jq}/bin/jq -r .BackendState)
        case "$state" in Running|Stopped|Starting) exit 0;; *) exit 1;; esac
      '';
    # This kernel lacks NF_CONNTRACK_MARK. Tailscale's supported external
    # firewall mode avoids its unconditional connmark workaround; peer ACLs
    # remain enforced by Tailscale before packets reach the TUN interface.
    networking.nftables.enable = true;
    networking.firewall = {
      enable = true;
      trustedInterfaces = [ config.services.tailscale.interfaceName ];
      filterForward = true;
      extraForwardRules = ''
        iifname ${tunnel} oifname ${interfaceSet} ip daddr ${subnetSet} counter accept
      '';
    };
    networking.nftables.tables.steamlink-tailnet-guard = {
      family = "inet";
      content = ''
        chain reject-spoofed-tailnet {
          type filter hook prerouting priority raw; policy accept;
          iifname != { "lo", ${tunnel} } ip saddr 100.64.0.0/10 counter drop
          iifname != { "lo", ${tunnel} } ip6 saddr fd7a:115c:a1e0::/48 counter drop
        }
      '';
    };
    networking.nftables.tables.steamlink-tailnet-nat = {
      family = "ip";
      content = ''
        chain postrouting {
          type nat hook postrouting priority srcnat; policy accept;
          iifname ${tunnel} oifname ${interfaceSet} ip saddr 100.64.0.0/10 ip daddr ${subnetSet} counter masquerade
        }
      '';
    };
    environment.systemPackages = [
      pkgs.curl
      pkgs.jq
      pkgs.iproute2
    ];
  };
}
