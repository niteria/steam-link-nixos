#!/bin/sh
# Host the vendor PE for HDMI audio without a framebuffer presenter or display
# owner. Unlike console/run.sh, the verified vendor tree is bind-mounted
# read-only into a small tmpfs root instead of being copied into RAM; only
# /tmp, /run and the SDK policy files are writable. The PCM FIFO directory is
# bound into the root at /run/pcm for the sink. PE is not restarted in place:
# a warm vendor re-initialization has never been qualified, and on this board
# it stalls early in PE init. A /run marker therefore refuses a second PE
# start in the same boot; checks that fail before the launch may be retried.
set -eu
VENDOR=${STEAMLINK_VENDOR_ROOT:-/run/steamlink/lower/opt/steamlink-vendor}
SUMS=${STEAMLINK_VENDOR_SUMS:-/run/steamlink/lower/opt/steamlink-vendor.sha256}
PCM=${STEAMLINK_PCM_DIR:?missing PCM FIFO directory}
WORK=/run/steamlink-audio
ROOT=$WORK/root
STARTED=/run/steamlink-pe-started
if [ -e "$STARTED" ]; then
    printf 'steamlink-audio: PE already ran in this boot (%s); reboot to restore HDMI audio\n' "$(cat "$STARTED")" >&2
    exit 1
fi
for attempt in $(seq 1 30); do
    [ ! -r /sys/class/mtd/mtd0/name ] || break
    sleep 1
done
if [ "${STEAMLINK_ROOT_MODE:-}" = ubifs-reclaimed ]; then
    : "${STEAMLINK_RECLAIM_CHECK:?missing reclaimed-root admission helper}"
    "$STEAMLINK_RECLAIM_CHECK"
else
    [ "$(cat /sys/class/mtd/mtd0/name)" = steamlink-internal-root-ro ]
    [ "$(cat /sys/class/mtd/mtd0/flags)" = 0x4000 ]
    [ "$(cat /sys/class/ubi/ubi0/ro_mode)" = 1 ]
    [ "$(cat /sys/class/mtd/mtd1/name)" = steamlink-stock-bootloader-ro ]
    [ "$(cat /sys/class/mtd/mtd1/size)" = 8388608 ]
    [ "$(cat /sys/class/mtd/mtd1/flags)" = 0x0 ]
fi
cache_node=/sys/firmware/devicetree/base/reserved-memory/av-cache@12000000
if [ ! -e "$cache_node/no-map" ]; then
    [ "$(tr -d '\000' < "$cache_node/compatible")" = shared-dma-pool ]
    [ -e "$cache_node/reusable" ]
fi
[ -e /sys/firmware/devicetree/base/reserved-memory/av-noncache@1e000000/no-map ]
[ -e /sys/firmware/devicetree/base/reserved-memory/av-low@0/no-map ]
code_start=$(awk '/Kernel code$/ {split($1,range,"-"); print range[1]}' /proc/iomem)
[ -n "$code_start" ] && [ "$((0x$code_start))" -ge 16777216 ]
[ -d "$PCM" ] && [ ! -L "$PCM" ]
case "${PE_USER_DEFAULT_RESOLUTION:-}" in
    720p|1080p|'') ;;
    *) printf 'Unsupported HDMI preference: %s\n' "$PE_USER_DEFAULT_RESOLUTION" >&2; exit 1 ;;
esac
# A previous instance killed before its own cleanup may have left mounts.
if [ -e "$WORK" ]; then
    ! mountpoint -q "$ROOT" || umount -R "$ROOT"
    rm -rf --one-file-system "$WORK"
fi
(cd "$VENDOR"; sha256sum --quiet -c "$SUMS")
PEPID=
cleanup() {
    trap - EXIT INT TERM
    if [ -n "$PEPID" ]; then
        kill -TERM "$PEPID" 2>/dev/null || true
        for attempt in $(seq 1 50); do kill -0 "$PEPID" 2>/dev/null || break; sleep 0.1; done
        kill -KILL "$PEPID" 2>/dev/null || true
    fi
    if mountpoint -q "$ROOT"; then umount -R "$ROOT" || umount -R -l "$ROOT" || true; fi
    mountpoint -q "$ROOT" || rm -rf --one-file-system "$WORK"
}
trap cleanup EXIT
trap 'cleanup; exit 0' INT TERM
mkdir -m 700 "$WORK"
mkdir "$ROOT"
mount -t tmpfs -o size=1m,mode=755,nosuid,nodev steamlink-audio "$ROOT"
for dir in bin lib usr home; do
    mkdir "$ROOT/$dir"
    mount --bind "$VENDOR/$dir" "$ROOT/$dir"
    mount -o remount,bind,ro,nosuid,nodev "$ROOT/$dir"
done
for dir in data dev proc sys tmp run var/log mnt/config/system; do mkdir -p "$ROOT/$dir"; done
printf 'marvell\n' > "$ROOT/mnt/config/system/audio_driver.txt"
# PE's startup preference and the SDL bridge have separate selectors.
[ -z "${PE_USER_DEFAULT_RESOLUTION:-}" ] ||
    printf '%s\n' "$PE_USER_DEFAULT_RESOLUTION" > "$ROOT/mnt/config/system/display_resolution.txt"
modprobe steamlink_av_shmcc enable=1
modprobe i2c_designware_platform
udevadm settle
modprobe steamlink_twsi enable=1 activate_bus=1
modprobe steamlink_pe enable=1
modprobe steamlink_gpio enable=1
udevadm settle
for path in galois_pe galois_pe_agent galois_cc shm_cache shm_noncache twsi0; do [ -c "/dev/$path" ]; done
[ "$(cat /sys/module/steamlink_pe/parameters/faulted)" = N ]
for dir in dev proc sys; do mount --rbind "/$dir" "$ROOT/$dir"; mount --make-rslave "$ROOT/$dir"; done
mount -t tmpfs -o size=16m,mode=1777,nosuid,nodev tmpfs "$ROOT/tmp"
mount -t tmpfs -o size=4m,mode=755,nosuid,nodev tmpfs "$ROOT/run"
mkdir "$ROOT/run/pcm"
mount --bind "$PCM" "$ROOT/run/pcm"
date -u +%FT%TZ > "$STARTED"
chroot "$ROOT" /bin/busybox sh -c 'cd /home/steam; export LD_LIBRARY_PATH=/home/steam/lib:/usr/lib:/lib; exec /lib/ld-linux-armhf.so.3 ./bin/PE_Single_CPU' &
PEPID=$!
# PE creates this FIFO right after installing its RPC service, i.e. when SDK
# clients may connect. (/tmp/pe_ready is only its boot-logo handshake.) Allow
# for slow HDMI/EDID initialization: PE cannot be retried in this boot.
ready=$ROOT/data/.PE_AV_Init.done
started=$(date +%s)
for attempt in $(seq 1 1500); do
    kill -0 "$PEPID"
    [ ! -p "$ready" ] || break
    sleep 0.2
done
[ -p "$ready" ]
printf 'steamlink-audio: PE %s ready after %s s\n' "$PEPID" "$(( $(date +%s) - started ))"
systemd-notify --ready
wait "$PEPID"
