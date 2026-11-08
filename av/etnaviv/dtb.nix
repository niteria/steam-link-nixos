{ pkgs, base }:
pkgs.runCommand "steamlink-etnaviv-dtb" { nativeBuildInputs = [ pkgs.dtc ]; } ''
  mkdir -p "$out"
  dtb="$out/berlin2cd-av-reserved.dtb"
  cp ${base}/berlin2cd-av-reserved.dtb "$dtb"
  chmod u+w "$dtb"
  clk=$(fdtget -t x "$dtb" /soc@f7000000/chip-control@ea0000/clock phandle)
  gic=$(fdtget -t x "$dtb" /soc@f7000000/interrupt-controller@ad1000 phandle)
  # reg is relative to the existing soc bus's f7000000 translation.
  node=/soc@f7000000/gpu@bc0000
  fdtput -c "$dtb" "$node"
  fdtput -t s "$dtb" "$node" compatible vivante,gc
  fdtput -t x "$dtb" "$node" reg bc0000 800
  fdtput -t x "$dtb" "$node" interrupt-parent "$gic"
  fdtput -t x "$dtb" "$node" interrupts 0 5 4
  # All three are owned before Etnaviv accesses its registers. Stock names are
  # gfx3d_sys/core/extra, exposed as bus/core/shader to the upstream driver.
  fdtput -t x "$dtb" "$node" clocks "$clk" 11 "$clk" 10 "$clk" 16
  fdtput -t s "$dtb" "$node" clock-names bus core shader
  fdtput -t s "$dtb" "$node" status okay
  test "$(fdtget -t x "$dtb" "$node" reg)" = 'bc0000 800'
  test "$(fdtget -t x "$dtb" "$node" interrupts)" = '0 5 4'
  dtc -I dtb -O dts "$dtb" > "$out/decoded.dts"
''
