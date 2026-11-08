{ pkgs, armPkgs }:
pkgs.runCommand "steamlink-local-diagnostics" {
  nativeBuildInputs = [ pkgs.python3 pkgs.ffmpeg-headless pkgs.espeak-ng pkgs.qemu pkgs.binutils ];
} ''
  mkdir -p "$out/bin" "$out/share/steamlink-diagnostics"
  cp ${./edid.h} edid.h
  ${armPkgs.pkgsStatic.stdenv.cc}/bin/${armPkgs.pkgsStatic.stdenv.cc.targetPrefix}cc \
    -static -std=gnu11 -Os -Wall -Wextra -Werror -I. ${./diagnostics.c} -o "$out/bin/steamlink-diagnostics"
  python3 - "$out/bin/steamlink-diagnostics" <<'PY'
import subprocess,sys
assert 'INTERP' not in subprocess.check_output(['readelf','-l',sys.argv[1]],text=True), 'diagnostic client must be standalone'
PY
  python3 ${./test-diagnostics.py} --client "$out/bin/steamlink-diagnostics" --qemu ${pkgs.qemu}/bin/qemu-arm
  espeak-ng -s 145 -w voice.wav 'Audio sample. Voices should sound clear and undistorted.'
  ffmpeg -nostdin -hide_banner -loglevel error -i voice.wav -af apad -t 5 -ar 48000 -ac 1 -f s16le mono.pcm
  python3 - "$out/share/steamlink-diagnostics/speech.pcm" <<'PY'
import array, pathlib, sys
samples=array.array('h',pathlib.Path('mono.pcm').read_bytes())
if sys.byteorder!='little': samples.byteswap()
assert len(samples)==240000
peak=max(abs(value) for value in samples);assert peak
output=array.array('h')
for value in samples:
    scaled=int(value*2048/peak);output.extend((scaled,scaled))
assert max(abs(value) for value in output)<=2048
if sys.byteorder!='little':output.byteswap()
pathlib.Path(sys.argv[1]).write_bytes(output.tobytes())
PY
  printf '%s\n' 'Generated local speech sample: stereo16-bit48000Hz,5seconds,peak2048(-24dBFS).' > "$out/share/steamlink-diagnostics/README.txt"
  printf '%s\n' marvell > "$out/share/steamlink-diagnostics/audio-route.txt"
''
