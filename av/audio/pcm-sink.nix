{
  pkgs,
  armPkgs,
  runtime,
}:
let
  cc = "${armPkgs.stdenv.cc}/bin/${armPkgs.stdenv.cc.targetPrefix}cc";
  # Linked at startup, as in the qualified media audio owner, so libc's
  # threading is initialized before the vendor agent starts its threads.
  pthread = pkgs.fetchurl {
    url = "https://raw.githubusercontent.com/ValveSoftware/steamlink-sdk/62b4d098d1472c3534dd098ca2a0e0e10712f1c6/rootfs/lib/libpthread-2.19.so";
    hash = "sha256-OOExsgVSvjkVIqfMHg88WUO4nt7iT1l8Hymxx4FR32s=";
  };
in
pkgs.runCommand "steamlink-pcm-sink"
  {
    nativeBuildInputs = [
      pkgs.stdenv.cc
      pkgs.patchelf
      pkgs.binutils
      pkgs.python3
    ];
  }
  ''
    mkdir -p "$out/bin" test
    python3 ${./resampler-coefficients.py} > resampler-coefficients.h
    printf 'marvell\n' > test/policy
    cc -shared -fPIC -std=gnu11 -O1 -Wall -Wextra -Werror -I${runtime}/usr/include \
      ${./mock-slaudio.c} -o test/libSLAudio.so.1
    printf '\n' | cc -shared -fPIC -x c - -o test/libshm.so
    cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all \
      -DPCM_SINK_POLICY="\"$PWD/test/policy\"" -DPCM_SINK_SHM="\"$PWD/test/libshm.so\"" \
      -DPCM_SINK_SLAUDIO="\"$PWD/test/libSLAudio.so.1\"" \
      -I. -I${runtime}/usr/include ${./pcm-sink.c} -ldl -o test/pcm-sink
    python3 ${./test-pcm-sink.py} test/pcm-sink "$PWD/test" 44100
    python3 ${./test-pcm-sink.py} test/pcm-sink "$PWD/test" 48000
    python3 ${./test-resampler.py} test/pcm-sink "$PWD/test"
    ${cc} -std=gnu11 -O2 -Wall -Wextra -Werror -fno-stack-protector -fno-pie \
      -I. -I${runtime}/usr/include -c ${./pcm-sink.c} -o pcm-sink.o
    ${cc} -nostdlib -no-pie -Wl,--dynamic-linker=/lib/ld-linux-armhf.so.3 \
      ${runtime}/usr/lib/crt1.o ${runtime}/usr/lib/crti.o pcm-sink.o \
      ${runtime}/usr/lib/libc_nonshared.a ${runtime}/lib/libdl-2.19.so ${pthread} \
      ${runtime}/lib/libc-2.19.so -lgcc ${runtime}/usr/lib/crtn.o -o "$out/bin/pcm-sink"
    patchelf --remove-rpath "$out/bin/pcm-sink"
    readelf --version-info "$out/bin/pcm-sink" > "$out/elf-versions.txt"
  ''
