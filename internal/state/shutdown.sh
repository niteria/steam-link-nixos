#!/bin/sh
# PID1 after systemd has stopped services and pivoted to the RAM shutdown tree.
PATH=/bin
export PATH
set -eu
hold() {
    printf 'State shutdown HELD: %s\n' "$*" >/dev/console
    while :; do sleep 3600; done
}
trap 'hold unexpected-exit' 0
[ -r /proc/mounts ] || mount -t proc proc /proc
[ -d /sys/class/mtd ] || mount -t sysfs sysfs /sys
[ -c /dev/console ] || mount -t devtmpfs devtmpfs /dev
sync
# Mount dependencies include bind-store, SquashFS loop backing, UBIFS and the
# old tmpfs root. Retry in descending path depth as loop references disappear.
for attempt in 1 2 3 4 5 6 7 8; do
    awk '{ print length($2), $2 }' /proc/mounts | sort -rn | while read -r depth target; do
        case "$target" in
            /oldroot|/oldroot/*|/run/steamlink/*) umount "$target" 2>/dev/null || true;;
        esac
    done
    losetup -D 2>/dev/null || true
done
if grep -q ' ubifs ' /proc/mounts; then hold 'UBIFS still mounted'; fi
ubidetach -m 0 || hold 'UBI detach'
if [ -d /sys/module/steamlink_marvell_state ]; then
    rmmod steamlink_marvell_state || hold 'physical writer unload'
elif [ -d /sys/module/steamlink_state_vm ]; then
    rmmod steamlink_state_vm || hold 'VM writer unload'
else
    hold 'writer missing'
fi
for path in /sys/class/mtd/mtd*/flags; do
    [ -f "$path" ] || continue
    flags=$(cat "$path")
    [ "$((flags & 1024))" = 0 ] || hold 'writable MTD remains'
done
printf 'PASS: state shutdown unmounted UBIFS, detached UBI and unloaded writer\n' >/dev/console
# An optional supervised session records disarm evidence on the qualified USB
# only after NAND has been detached and its writer removed.
receipt=
for word in $(cat /proc/cmdline); do
    case "$word" in steamlink.shutdown-session=*) [ -z "$receipt" ] || hold duplicate-receipt; receipt=${word#*=};; esac
done
if [ -n "$receipt" ]; then
    [ "${#receipt}" = 16 ] || hold receipt-length
    case "$receipt" in *[!0-9a-f]*) hold receipt-syntax;; esac
    for attempt in 1 2 3 4 5 6 7 8 9 10; do
        [ -r /sys/block/sda/device/../../../../serial ] && break
        sleep 1
    done
    [ "$(cat /sys/block/sda/device/../../../../serial)" = @RECOVERY_USB_SERIAL@ ] || hold receipt-drive
    mkdir -p /usb
    mount -t vfat /dev/sda1 /usb || hold receipt-mount
    [ ! -e "/usb/boot/runtime-shutdown-$receipt" ] || hold receipt-exists
    printf 'state-shutdown-disarmed %s %s\n' "$receipt" "$(cat /proc/sys/kernel/random/boot_id)" >"/usb/boot/runtime-shutdown-$receipt"
    sync
    umount /usb || hold receipt-unmount
fi
trap - 0
case "${1:-}" in
    reboot|kexec) exec reboot -f;;
    poweroff|halt) exec poweroff -f;;
    *) hold 'unknown shutdown verb';;
esac
