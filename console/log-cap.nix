{ stdenv }:
stdenv.mkDerivation {
  pname = "steamlink-log-cap";
  version = "0.1";
  src = ./log-cap.c;
  dontUnpack = true;
  buildPhase = ''$CC -std=c11 -O2 -Wall -Wextra -Werror "$src" -o log-cap'';
  installPhase = ''install -Dm755 log-cap "$out/bin/log-cap"'';
}
