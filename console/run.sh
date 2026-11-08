#!/bin/sh
# Present actual Linux fbcon pixels. This image's NAND adapter is read-only.
set -eu
VENDOR=${STEAMLINK_VENDOR_ROOT:-/run/steamlink/lower/opt/steamlink-vendor}
SUMS=${STEAMLINK_VENDOR_SUMS:-/run/steamlink/lower/opt/steamlink-vendor.sha256}
WORK=/run/steamlink-console
[ "$#" = 1 ] || [ "$#" = 2 ]
PRESENTER=$1
if [ ! -c /dev/fb0 ]; then
    modprobe vfb vfb_enable=1 mode_option=640x480-32 videomemorysize=2097152
fi
for attempt in $(seq 1 30); do
    [ ! -r /sys/class/mtd/mtd0/name ] || [ ! -c /dev/fb0 ] || break
    sleep 1
done
if [ "$#" = 2 ]; then
    [ "$(cat /sys/class/mtd/mtd0/name)" = steamlink-nand-ro ]
    [ "$(cat /sys/class/mtd/mtd0/flags)" = 0x0 ]
    [ ! -e /sys/class/ubi/ubi0 ]
    modprobe steamlink_pe_flash
    VENDOR=$2/root
    SUMS=$2/SHA256SUMS
    (cd "$2"; sha256sum -c "$SUMS")
elif [ "${STEAMLINK_ROOT_MODE:-}" = ubifs-reclaimed ]; then
    : "${STEAMLINK_RECLAIM_CHECK:?missing reclaimed-root admission helper}"
    "$STEAMLINK_RECLAIM_CHECK"
elif [ "${STEAMLINK_ROOT_MODE:-}" = ubifs-rw ]; then
    [ "$(cat /sys/class/mtd/mtd0/name)" = steamlink-internal-state-root ]
    [ "$(cat /sys/class/mtd/mtd0/flags)" = 0x4400 ]
    [ "$(cat /sys/class/ubi/ubi0/ro_mode)" = 0 ]
    [ "$(cat /sys/class/ubi/ubi0_1/reserved_ebs)" = 900 ]
    [ "$(cat /sys/class/mtd/mtd2/name)" = steamlink-state-boot-pools-ro ]
    for number in 1 2 3 4; do [ "$(cat /sys/class/mtd/mtd$number/flags)" = 0x0 ]; done
else
    [ "$(cat /sys/class/mtd/mtd0/name)" = steamlink-internal-root-ro ]
    [ "$(cat /sys/class/mtd/mtd0/flags)" = 0x4000 ]
    [ "$(cat /sys/class/ubi/ubi0/ro_mode)" = 1 ]
fi
if [ "${STEAMLINK_ROOT_MODE:-}" != ubifs-rw ] && [ "${STEAMLINK_ROOT_MODE:-}" != ubifs-reclaimed ]; then
    [ "$(cat /sys/class/mtd/mtd1/name)" = steamlink-stock-bootloader-ro ]
    [ "$(cat /sys/class/mtd/mtd1/size)" = 8388608 ]
    [ "$(cat /sys/class/mtd/mtd1/flags)" = 0x0 ]
fi
[ -c /dev/fb0 ] && [ -c /dev/tty1 ]
cache_node=/sys/firmware/devicetree/base/reserved-memory/av-cache@12000000
if [ ! -e "$cache_node/no-map" ]; then
    [ "$(tr -d '\000' < "$cache_node/compatible")" = shared-dma-pool ]
    [ -e "$cache_node/reusable" ]
fi
[ -e /sys/firmware/devicetree/base/reserved-memory/av-noncache@1e000000/no-map ]
[ -e /sys/firmware/devicetree/base/reserved-memory/av-low@0/no-map ]
code_start=$(awk '/Kernel code$/ {split($1,range,"-"); print range[1]}' /proc/iomem)
[ -n "$code_start" ] && [ "$((0x$code_start))" -ge 16777216 ]
[ ! -e "$WORK" ] && [ ! -L "$WORK" ]
mkdir -m 700 "$WORK"
ROOT=$WORK/root
mkdir "$ROOT"
cp -a "$VENDOR"/. "$ROOT/"
if [ "$#" = 1 ]; then (cd "$ROOT"; sha256sum -c "$SUMS"); fi
cp "$PRESENTER" "$ROOT/fb-console"; chmod 755 "$ROOT/fb-console"
if [ -n "${STEAMLINK_DIAGNOSTICS:-}" ]; then
    mkdir -p "$ROOT/opt"
    cp -a "$STEAMLINK_DIAGNOSTICS" "$ROOT/opt/steamlink-diagnostics"
    mkdir -p "$ROOT/mnt/config/system"
    cp "$STEAMLINK_DIAGNOSTICS/audio-route.txt" "$ROOT/mnt/config/system/audio_driver.txt"
fi
# PE's startup preference and the SDL bridge have separate selectors. Keep the
# latter in the disposable vendor root so creating a context does not undo it.
case "${PE_USER_DEFAULT_RESOLUTION:-}" in
    720p|1080p)
        mkdir -p "$ROOT/mnt/config/system"
        printf '%s\n' "$PE_USER_DEFAULT_RESOLUTION" > "$ROOT/mnt/config/system/display_resolution.txt"
        ;;
    '') ;;
    *) printf 'Unsupported HDMI preference: %s\n' "$PE_USER_DEFAULT_RESOLUTION" >&2; exit 1 ;;
esac
PEPID=; FBPID=
cleanup() {
    trap - EXIT INT TERM
    for pid in "$FBPID" "$PEPID"; do
        [ -z "$pid" ] || kill -TERM "$pid" 2>/dev/null || true
    done
    for dir in run tmp sys proc dev; do umount -R "$ROOT/$dir" 2>/dev/null || true; done
}
trap cleanup EXIT INT TERM
modprobe steamlink_av_shmcc enable=1
modprobe i2c_designware_platform
udevadm settle
modprobe steamlink_twsi enable=1 activate_bus=1
modprobe steamlink_pe enable=1
modprobe steamlink_gpio enable=1
udevadm settle
for path in galois_pe galois_pe_agent galois_cc shm_cache shm_noncache twsi0; do [ -c "/dev/$path" ]; done
for dir in dev proc sys tmp run; do mkdir -p "$ROOT/$dir"; done
for dir in dev proc sys; do mount --rbind "/$dir" "$ROOT/$dir"; mount --make-rslave "$ROOT/$dir"; done
mount -t tmpfs -o size=16m,mode=1777 tmpfs "$ROOT/tmp"
run_size=4m
[ "${STEAMLINK_HEADLESS:-0}" != 1 ] || run_size=16m
mount -t tmpfs -o size=$run_size,mode=755 tmpfs "$ROOT/run"
mkdir -m 700 "$ROOT/run/media-player"
chroot "$ROOT" /bin/busybox sh -c 'cd /home/steam; export LD_LIBRARY_PATH=/home/steam/lib:/usr/lib:/lib; exec /lib/ld-linux-armhf.so.3 ./bin/PE_Single_CPU' >>"$WORK/pe.log" 2>&1 &
PEPID=$!
printf '%s\n' "$PEPID" >"$WORK/pe.pid"
sleep 20
kill -0 "$PEPID"
if [ "${STEAMLINK_HEADLESS:-0}" = 1 ]; then
    # Dedicated media/display units are ordered after this readiness boundary.
    # They stop before this service; PE remains supervised without SIGSTOP tricks.
    systemd-notify --ready
    wait "$PEPID"
    exit $?
fi
chvt 1
printf '\n[steamlink] NAND Linux framebuffer console: fb0 / tty1\n' >/dev/tty1
chroot "$ROOT" /bin/busybox sh -c 'export LD_LIBRARY_PATH=/home/steam/lib:/usr/lib:/lib; export STEAMLINK_FB_READY=/run/framebuffer-ready; export STEAMLINK_MEDIA_CONTROL=/run/media-player; exec /lib/ld-linux-armhf.so.3 /fb-console /dev/fb0' >"$WORK/presenter.log" 2>&1 &
FBPID=$!
for attempt in $(seq 1 200); do
    kill -0 "$PEPID"; kill -0 "$FBPID"
    if [ -f "$ROOT/run/framebuffer-ready" ]; then systemd-notify --ready; break; fi
    sleep 1
done
[ -f "$ROOT/run/framebuffer-ready" ]
wait "$FBPID"
