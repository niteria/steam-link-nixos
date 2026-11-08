{
  pkgs,
  kernel,
  ethernetAddress,
  withPe ? false,
  reserveLow ? false,
}:
pkgs.runCommand "steamlink-av-reserved-memory-dtb"
  {
    nativeBuildInputs = [ pkgs.dtc ] ++ pkgs.lib.optional withPe pkgs.python3;
  }
  ''
    mkdir -p "$out"
    cp ${kernel}/dtbs/berlin2cd-valve-steamlink.dtb "$out/berlin2cd-av-reserved.dtb"
    chmod u+w "$out/berlin2cd-av-reserved.dtb"
    # Only the two SDK-described AV heaps are reserved. The current modern
    # kernel occupies 0x00008000..0x00e591eb; reserving stock's entire low 16 MiB
    # would overlap it. This artifact does not claim to preserve old PE state.
    fdtput -c "$out/berlin2cd-av-reserved.dtb" /reserved-memory
    fdtput -t x "$out/berlin2cd-av-reserved.dtb" /reserved-memory '#address-cells' 1
    fdtput -t x "$out/berlin2cd-av-reserved.dtb" /reserved-memory '#size-cells' 1
    fdtput "$out/berlin2cd-av-reserved.dtb" /reserved-memory ranges
    ${pkgs.lib.optionalString reserveLow ''
      # ARM's AUTO_ZRELADDR validates its guessed base against this actual RAM
      # range. Excluding low RAM (rather than only reserving it) relocates the
      # uncompressed kernel and its early page tables above 16 MiB.
      fdtput -t x "$out/berlin2cd-av-reserved.dtb" /memory@0 reg 1000000 1f000000
      fdtput -t x "$out/berlin2cd-av-reserved.dtb" /memory@0 linux,usable-memory 1000000 1f000000
      fdtput -p -t x "$out/berlin2cd-av-reserved.dtb" /chosen linux,usable-memory-range 1000000 1f000000
      fdtput -c "$out/berlin2cd-av-reserved.dtb" /reserved-memory/av-low@0
      fdtput -t x "$out/berlin2cd-av-reserved.dtb" /reserved-memory/av-low@0 reg 0 1000000
      fdtput "$out/berlin2cd-av-reserved.dtb" /reserved-memory/av-low@0 no-map
    ''}
    fdtput -c "$out/berlin2cd-av-reserved.dtb" /reserved-memory/av-cache@12000000
    fdtput -t x "$out/berlin2cd-av-reserved.dtb" /reserved-memory/av-cache@12000000 reg 12000000 c000000
    fdtput "$out/berlin2cd-av-reserved.dtb" /reserved-memory/av-cache@12000000 no-map
    fdtput -c "$out/berlin2cd-av-reserved.dtb" /reserved-memory/av-noncache@1e000000
    fdtput -t x "$out/berlin2cd-av-reserved.dtb" /reserved-memory/av-noncache@1e000000 reg 1e000000 2000000
    fdtput "$out/berlin2cd-av-reserved.dtb" /reserved-memory/av-noncache@1e000000 no-map
    ${pkgs.lib.optionalString withPe ''
      dtb="$out/berlin2cd-av-reserved.dtb"
      gic=$(fdtget -t x "$dtb" /soc@f7000000/interrupt-controller@ad1000 phandle)
      sic=$(fdtget -t x "$dtb" /soc@f7000000/apb@fc0000/interrupt-controller@e000 phandle)
      clk=$(fdtget -t x "$dtb" /soc@f7000000/chip-control@ea0000/clock phandle)
      fdtput -c "$dtb" /steamlink-pe
      fdtput -t s "$dtb" /steamlink-pe compatible mrvl,berlin-pe
      fdtput -t x "$dtb" /steamlink-pe interrupt-parent "$gic"
      fdtput -t x "$dtb" /steamlink-pe interrupts 0 13 4 0 0 4 0 1 4 0 19 4 0 2 4
      # IDs are from Linux's dt-bindings/clock/berlin2.h. Keep AV clocks owned
      # through the clock API; reporting their rate does not enable a gated block.
      fdtput -t x "$dtb" /steamlink-pe clocks "$clk" 2 "$clk" 4 "$clk" 5 "$clk" 7 "$clk" 8 "$clk" a "$clk" b "$clk" c "$clk" d "$clk" e "$clk" f "$clk" 13 "$clk" 17 "$clk" 24 "$clk" 25 "$clk" 26 "$clk" 27 "$clk" 3
      fdtput -t s "$dtb" /steamlink-pe clock-names drmfigo gfx zsp pcube vscope vpp app audio0 audio2 audio3 audio1 vip gc360 audiohd video0 video1 video2 cfg
      fdtput -c "$dtb" /steamlink-pe/cec
      fdtput -t s "$dtb" /steamlink-pe/cec compatible mrvl,berlin-cec
      fdtput -t x "$dtb" /steamlink-pe/cec interrupt-parent "$sic"
      fdtput -t x "$dtb" /steamlink-pe/cec interrupts 10
      # Keep I2C0 disabled during boot. Its explicit adapter activation is a later
      # runtime checkpoint, after Ethernet/SSH recovery is confirmed.
      fdtput -t s "$dtb" /soc@f7000000/apb@e80000/i2c@1400 status disabled
      fdtput -t i "$dtb" /soc@f7000000/apb@e80000/i2c@1400 clock-frequency 400000
      test "$(fdtget -t x "$dtb" /steamlink-pe interrupts)" = '0 13 4 0 0 4 0 1 4 0 19 4 0 2 4'
      python3 ${./audit-candidate-dtb.py} --kernel-source ${kernel.src} --dtb "$dtb" --output "$out/clock-binding-verification.json"
    ''}
    fdtput -t bx "$out/berlin2cd-av-reserved.dtb" /soc@f7000000/ethernet@b90000 local-mac-address ${pkgs.lib.concatStringsSep " " (pkgs.lib.splitString ":" (pkgs.lib.toLower ethernetAddress))}
    test "$(fdtget -t x "$out/berlin2cd-av-reserved.dtb" /memory@0 reg)" = '${
      if reserveLow then "1000000 1f000000" else "0 20000000"
    }'
    test "$(fdtget -t x "$out/berlin2cd-av-reserved.dtb" /reserved-memory/av-cache@12000000 reg)" = '12000000 c000000'
    test "$(fdtget -t x "$out/berlin2cd-av-reserved.dtb" /reserved-memory/av-noncache@1e000000 reg)" = '1e000000 2000000'
    fdtget "$out/berlin2cd-av-reserved.dtb" /reserved-memory/av-cache@12000000 no-map
    fdtget "$out/berlin2cd-av-reserved.dtb" /reserved-memory/av-noncache@1e000000 no-map
    dtc -I dtb -O dts "$out/berlin2cd-av-reserved.dtb" > "$out/decoded.dts"
    cat > "$out/verification.json" <<'EOF'
    {"status":"offline-validated","physical_ram_bytes":536870912,"reserved_av_bytes":${
      if reserveLow then "251658240" else "234881024"
    },"remaining_ram_bytes":${if reserveLow then "285212672" else "301989888"},"low_16m_reserved":${
      if reserveLow then "true" else "false"
    },"booted":false,"pe_nodes_included":${
      if withPe then "true" else "false"
    },"av_driver_included":false}
    EOF
  ''
