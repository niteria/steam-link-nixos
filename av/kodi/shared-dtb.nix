{ pkgs, base }:
pkgs.runCommand "steamlink-shared-display-dtb" { nativeBuildInputs = [ pkgs.dtc ]; } ''
  mkdir -p "$out"
  dtb="$out/berlin2cd-av-reserved.dtb"
  cp ${base}/berlin2cd-av-reserved.dtb "$dtb"
  chmod u+w "$dtb"
  cache=/reserved-memory/av-cache@12000000
  fdtput -d "$dtb" "$cache" no-map
  fdtput -t s "$dtb" "$cache" compatible shared-dma-pool
  fdtput "$dtb" "$cache" reusable
  fdtput -t x "$dtb" "$cache" alignment 400000
  fdtput -t x "$dtb" "$cache" phandle 53484d
  fdtput -c "$dtb" /steamlink-shm-dma
  fdtput -t s "$dtb" /steamlink-shm-dma compatible valve,steamlink-shm-dma
  fdtput -t x "$dtb" /steamlink-shm-dma memory-region 53484d
  test "$(fdtget -t x "$dtb" "$cache" reg)" = '12000000 c000000'
  test "$(fdtget -t s "$dtb" "$cache" compatible)" = shared-dma-pool
  dtc -I dtb -O dts "$dtb" > "$out/decoded.dts"
''
