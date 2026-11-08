{ pkgs }:
let
  fonts = pkgs.dejavu_fonts;
  fontconfig = pkgs.makeFontsConf { fontDirectories = [ fonts ]; };
  coreFonts = pkgs.runCommand "steamlink-xorg-core-fonts" { } ''
    mkdir -p "$out"
    cp ${pkgs.font-misc-misc}/share/fonts/X11/misc/6x13.pcf.gz "$out/fixed.pcf.gz"
    cp ${pkgs.font-cursor-misc}/share/fonts/X11/misc/cursor.pcf.gz "$out/cursor.pcf.gz"
    printf '%s\n' 2 \
      'fixed.pcf.gz -misc-fixed-medium-r-semicondensed--13-120-75-75-c-60-iso10646-1' \
      'cursor.pcf.gz cursor' > "$out/fonts.dir"
    printf '%s\n' 'fixed -misc-fixed-medium-r-semicondensed--13-120-75-75-c-60-iso10646-1' > "$out/fonts.alias"
  '';
  # nixpkgs' extra setlayout helper uses unprefixed gcc/pkg-config, which are
  # unavailable when cross-compiling. Use the toolchain selected by stdenv.
  openbox = pkgs.openbox.overrideAttrs (old: {
    postBuild = ''$CC -O2 -o setlayout $($PKG_CONFIG --cflags --libs x11) $setlayoutSrc'';
    # This proof invokes the WM directly. Session/autostart helpers and SDK
    # metadata otherwise pull Python and development closures into the RAM image.
    postFixup = (old.postFixup or "") + ''
      rm -rf "$out/libexec" "$out/lib/pkgconfig" "$out/nix-support" "$out/share/man"
      rm -f "$out"/lib/*.la
      for program in "$out"/bin/* "$out"/bin/.*; do
        case "$program" in */openbox|*/.openbox-wrapped|*/setlayout) ;;
          *) if [ -f "$program" ]; then rm -f "$program"; fi;;
        esac
      done
    '';
  });
  modules = pkgs.buildEnv {
    name = "steamlink-xorg-modules";
    paths = [ pkgs.xorg-server pkgs.xf86-video-fbdev pkgs.xf86-input-libinput ];
    pathsToLink = [ "/lib/xorg/modules" ];
  };
  config = pkgs.writeText "steamlink-xorg.conf" ''
    Section "Files"
      FontPath "${coreFonts}"
    EndSection
    Section "Module"
      Load "fbdevhw"
      Load "shadow"
    EndSection
    Section "ServerFlags"
      Option "AutoAddDevices" "true"
      Option "DontZap" "false"
    EndSection
    Section "Device"
      Identifier "SteamLinkFramebuffer"
      Driver "fbdev"
      Option "fbdev" "/dev/fb0"
      Option "ShadowFB" "true"
    EndSection
    Section "Monitor"
      Identifier "SteamLinkMonitor"
    EndSection
    Section "Screen"
      Identifier "SteamLinkScreen"
      Device "SteamLinkFramebuffer"
      Monitor "SteamLinkMonitor"
      DefaultDepth 24
    EndSection
    Section "InputClass"
      Identifier "SteamLinkKeyboard"
      MatchIsKeyboard "on"
      Driver "libinput"
    EndSection
    Section "InputClass"
      Identifier "SteamLinkPointer"
      MatchIsPointer "on"
      Driver "libinput"
    EndSection
  '';
in pkgs.writeShellScriptBin "steamlink-desktop-proof" ''
  set -eu
  export HOME=/root DISPLAY=:1 XAUTHORITY=/run/desktop.Xauthority
  export XDG_RUNTIME_DIR=/run/desktop-user
  export FONTCONFIG_FILE=${fontconfig}
  export XDG_CONFIG_DIRS=${openbox}/etc/xdg
  export SHELL=${pkgs.bashInteractive}/bin/bash
  export XKB_CONFIG_ROOT=${pkgs.xkeyboard_config}/share/X11/xkb
  export PATH=${pkgs.lib.makeBinPath [ pkgs.coreutils pkgs.xauth pkgs.xorg-server openbox pkgs.xterm pkgs.xsetroot pkgs.xdpyinfo pkgs.xinput pkgs.xwininfo pkgs.xprop pkgs.xdotool ]}
  mkdir -p "$HOME" "$XDG_RUNTIME_DIR" /tmp/.X11-unix
  chmod 700 "$HOME" "$XDG_RUNTIME_DIR"
  chmod 1777 /tmp /tmp/.X11-unix
  cookie=$(od -An -N16 -tx1 /dev/urandom | tr -d ' \n')
  touch "$XAUTHORITY"; chmod 600 "$XAUTHORITY"
  xauth -f "$XAUTHORITY" add :1 MIT-MAGIC-COOKIE-1 "$cookie"
  server=; wm=; terminal=
  cleanup() {
    status=$?
    trap - EXIT INT TERM
    for pid in "$terminal" "$wm" "$server"; do
      if [ -n "$pid" ]; then kill -TERM "$pid" 2>/dev/null || true; fi
    done
    if [ -n "$server" ]; then wait "$server" 2>/dev/null || true; fi
    printf '%s\n' "$status" > /run/desktop-exit-status
  }
  trap cleanup EXIT INT TERM
  Xorg :1 vt8 -keeptty -noreset -nolisten tcp -auth "$XAUTHORITY" \
    -config ${config} -modulepath ${modules}/lib/xorg/modules \
    -logfile /run/Xorg.log > /run/Xorg.stdout 2>&1 &
  server=$!
  ready=false
  for attempt in $(seq 1 100); do
    kill -0 "$server"
    if xdpyinfo > /run/xdpyinfo.txt 2>/dev/null; then ready=true; break; fi
    sleep 0.1
  done
  "$ready"
  xsetroot -solid '#203040'
  openbox --sm-disable > /run/openbox.log 2>&1 & wm=$!
  xterm -fa 'DejaVu Sans Mono' -fs 11 -geometry 64x18+12+12 \
    -title 'Steam Link desktop proof - exit terminal to return' \
    -e ${pkgs.bashInteractive}/bin/bash --noprofile --norc & terminal=$!
  xinput list > /run/input-devices.txt
  mapped=false
  deadline=$((SECONDS+75))
  while [ "$SECONDS" -lt "$deadline" ]; do
    kill -0 "$terminal"; kill -0 "$wm"
    xwininfo -root -tree > /run/windows.txt
    manager=$(xprop -root _NET_SUPPORTING_WM_CHECK)
    case "$manager" in
      *'window id #'*)
        if xdotool search --onlyvisible --name '^Steam Link desktop proof - exit terminal to return$' > /run/terminal-window-id; then
          mapped=true; break
        fi;;
    esac
    sleep 0.2
  done
  "$mapped"
  printf '%s\n' 'desktop-ready' > /run/desktop-ready
  if [ -n "''${NOTIFY_SOCKET:-}" ]; then ${pkgs.systemd}/bin/systemd-notify --ready; fi
  wait "$terminal"
''
