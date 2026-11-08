{ pkgs, armPkgs, runtime }:
let cc = "${armPkgs.stdenv.cc}/bin/${armPkgs.stdenv.cc.targetPrefix}cc";
in pkgs.runCommand "steamlink-fb-console-presenter" {
  nativeBuildInputs = [ pkgs.stdenv.cc pkgs.patchelf pkgs.binutils ];
} ''
  mkdir -p "$out/bin"
  cp ${./pixels.h} pixels.h
  cp ${./media.h} media.h
  cp ${./edid.h} edid.h
  cp ${./audio.h} audio.h
  cp ${./surfaces.h} surfaces.h
  cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I. -I${runtime}/usr/include ${./test-surfaces.c} -o surfaces-test
  ./surfaces-test
  cc -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined -I. ${./test-pixels.c} -o pixels-test
  ./pixels-test
  cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I. -I${runtime}/usr/include ${./test-media.c} -o media-test
  ./media-test
  cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I. -I${runtime}/usr/include ${./test-audio.c} -ldl -o audio-test
  ./audio-test
  ${cc} -std=gnu11 -O2 -Wall -Wextra -Werror -fno-stack-protector -fno-pie \
    -I. -I${runtime}/usr/include -c ${./presenter.c} -o presenter.o
  ${cc} -nostdlib -no-pie -Wl,--dynamic-linker=/lib/ld-linux-armhf.so.3 \
    ${runtime}/usr/lib/crt1.o ${runtime}/usr/lib/crti.o presenter.o \
    ${runtime}/usr/lib/libc_nonshared.a ${runtime}/lib/libdl-2.19.so \
    ${runtime}/lib/libc-2.19.so -lgcc ${runtime}/usr/lib/crtn.o -o "$out/bin/fb-console"
  patchelf --remove-rpath "$out/bin/fb-console"
  patchelf --set-interpreter /lib/ld-linux-armhf.so.3 "$out/bin/fb-console"
  readelf --version-info "$out/bin/fb-console" > "$out/elf-versions.txt"
''
