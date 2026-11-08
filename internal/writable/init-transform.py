"""Boot standard writable UBIFS; select the persistent Nix system profile."""
import re

nonce = os.environ["STATE_NONCE"]
assert re.fullmatch("[0-9a-f]{16}", nonce)
snapshot = os.environ["ROOT_SNAPSHOT"]
recovery_usb = os.environ["RECOVERY_USB_SERIAL"]
assert re.fullmatch("[0-9a-f]{64}", snapshot)
assert not recovery_usb or re.fullmatch(r"[A-Za-z0-9._-]{1,64}", recovery_usb)
script = r'''#!/bin/sh
PATH=/bin
export PATH
set -eu
writer=0
hold() { printf 'Writable-root boot HELD: %s\n' "$*" >/dev/console; while :; do sleep 3600; done; }
fail() {
    trap - 0
    printf 'Writable-root boot failed: %s\n' "$*" >/dev/console
    # Restore virtual mounts if a late switch_root setup step failed.
    if [ ! -r /proc/mounts ] && [ -r /newroot/proc/mounts ]; then mount --move /newroot/proc /proc || hold proc-return; fi
    if [ ! -d /sys/module ] && [ -d /newroot/sys/module ]; then mount --move /newroot/sys /sys || hold sys-return; fi
    if [ ! -c /dev/console ] && [ -c /newroot/dev/console ]; then mount --move /newroot/dev /dev || hold dev-return; fi
    if [ ! -x /run/busybox ] && [ -x /newroot/run/busybox ]; then mount --move /newroot/run /run || hold run-return; fi
    if [ "$writer" = 1 ]; then
        sync
        umount /newroot/run 2>/dev/null || true
        umount /newroot 2>/dev/null || true
        if [ -d /sys/class/ubi/ubi0 ]; then ubidetach -m 0 || hold ubi-detach; fi
        if [ -d /sys/module/steamlink_marvell_state ]; then rmmod steamlink_marvell_state || hold writer-unload; fi
        for path in /sys/class/mtd/mtd*/flags; do
            [ -f "$path" ] || continue
            flags=$(cat "$path"); [ "$((flags & 1024))" = 0 ] || hold writer-still-armed
        done
    fi
    printf 'Writable-root boot failure disarmed\n' >/dev/console
    # Recovery boot is allowed only after successful disarm, with the known USB.
    for attempt in 1 2 3 4 5 6 7 8 9 10; do
        if [ -r /sys/block/sda/device/../../../../serial ]; then break; fi
        sleep 1
    done
    if [ "$(cat /sys/block/sda/device/../../../../serial 2>/dev/null || true)" = @RECOVERY_USB_SERIAL@ ]; then exec reboot -f; fi
    hold recovery-usb-unavailable
}
trap 'fail unexpected-exit' 0
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
mount -t tmpfs -o mode=0755 tmpfs /run
cp -L /bin/sh /run/busybox
chmod 755 /run/busybox
modprobe vfb vfb_enable=1 mode_option=640x480-32 videomemorysize=2097152 || fail framebuffer
modprobe steamlink_nand_overlay || fail nand-overlay
writer=1
modprobe steamlink_marvell_state write_enable=1 confirm_start=2336227328 confirm_blocks=1858 state_nonce=0x@NONCE@ || fail nand-writer
[ "$(cat /sys/class/mtd/mtd0/name)" = steamlink-internal-state-root ] || fail mtd-name
[ "$(cat /sys/class/mtd/mtd0/size)" = 974127104 ] || fail mtd-size
[ "$(cat /sys/class/mtd/mtd0/flags)" = 0x4400 ] || fail mtd-flags
[ "$(cat /sys/module/steamlink_marvell_state/parameters/state_nonce)" = @NONCE_DEC@ ] || fail nonce
for number in 1 2 3 4; do [ "$(cat /sys/class/mtd/mtd$number/flags)" = 0x0 ] || fail protected-view; done
ubiattach -m 0 -d 0 -b 20 --disable-fastmap || fail ubi-attach
[ "$(cat /sys/class/ubi/ubi0/ro_mode)" = 0 ] || fail ubi-readonly
for spec in 0:root-a:448 1:root-b:900 2:state:272; do
    id=${spec%%:*}; rest=${spec#*:}; name=${rest%%:*}; count=${rest##*:}
    [ "$(cat /sys/class/ubi/ubi0_$id/name)" = "$name" ] || fail volume-name
    [ "$(cat /sys/class/ubi/ubi0_$id/reserved_ebs)" = "$count" ] || fail volume-size
done
mount -t ubifs -o rw,noatime,compr=zstd ubi0:root-b /newroot || fail root-mount
[ "$(cat /newroot/.steamlink-writable-root)" = 'steamlink-ubifs-root-v1 @SNAPSHOT@ @NONCE@ root-b 900' ] || fail root-marker
mkdir -p /newroot/etc/ssh /newroot/run
if [ -d /steamlink-identity ] && [ ! -L /steamlink-identity ]; then
    for name in ssh_host_ed25519_key ssh_host_ed25519_key.pub; do
        [ -f "/steamlink-identity/$name" ] && [ ! -L "/steamlink-identity/$name" ] || fail identity-source
        [ ! -L "/newroot/etc/ssh/$name" ] || fail identity-link
        if [ -f "/newroot/etc/ssh/$name" ]; then
            /run/busybox cmp "/steamlink-identity/$name" "/newroot/etc/ssh/$name" || fail identity-changed
        else cp "/steamlink-identity/$name" "/newroot/etc/ssh/$name"; fi
    done
    chmod 600 /newroot/etc/ssh/ssh_host_ed25519_key
    chmod 644 /newroot/etc/ssh/ssh_host_ed25519_key.pub
fi
# Reuse the already-qualified RAM shutdown/disarm environment.
mkdir -p /run/initramfs/bin /run/initramfs/lib /run/initramfs/nix /run/initramfs/proc /run/initramfs/sys /run/initramfs/dev /run/initramfs/run /run/initramfs/oldroot
cp -a /bin /lib /nix /run/initramfs/
cp /state-shutdown /run/initramfs/shutdown
chmod 755 /run/initramfs/shutdown
mount --bind /run /newroot/run
system=$(/run/busybox chroot /newroot /run/busybox readlink -f /nix/var/nix/profiles/system) || fail system-profile
umount /newroot/run
case "$system" in /nix/store/*-nixos-system-*) ;; *) fail invalid-system-profile;; esac
[ -x "/newroot$system/init" ] || fail missing-system-init
mkdir -p /run/steamlink
printf 'filesystem ubifs-rw\nvolume root-b\nsystem %s\n' "$system" >/run/steamlink/root-verified
sync
for part in proc sys dev run; do mount --move "/$part" "/newroot/$part" || fail virtual-mount-move; done
trap - 0
exec switch_root /newroot "$system/init"
'''.replace("@NONCE@", nonce).replace("@NONCE_DEC@", str(int(nonce, 16))).replace("@SNAPSHOT@", snapshot)
if recovery_usb:
    script = script.replace("@RECOVERY_USB_SERIAL@", recovery_usb)
else:
    script = script.replace('    if [ "$(cat /sys/block/sda/device/../../../../serial 2>/dev/null || true)" = @RECOVERY_USB_SERIAL@ ]; then exec reboot -f; fi',
                            '    # Automatic USB recovery reboot is not configured.')
