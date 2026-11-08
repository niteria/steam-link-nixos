#!/bin/sh
# PID 1 for the candidate read-only NAND base / RAM-state system.
PATH=/bin
export PATH
set -eu
hold() {
    printf 'Internal boot held; attach the qualified USB recovery drive and power-cycle.\n' >/dev/console
    while :; do sleep 3600; done
}
fail() { printf 'Internal initramfs FAILED: %s\n' "$*" >/dev/console; hold; }
trap 'fail "unexpected stage-1 exit"' 0
mkdir -p /proc /sys /dev /run /newroot
mount -t proc proc /proc || fail proc
mount -t sysfs sysfs /sys || fail sysfs
mount -t devtmpfs devtmpfs /dev || fail devtmpfs
[ -e /dev/fd ] || ln -s /proc/self/fd /dev/fd
[ -e /dev/stdin ] || ln -s /proc/self/fd/0 /dev/stdin
[ -e /dev/stdout ] || ln -s /proc/self/fd/1 /dev/stdout
[ -e /dev/stderr ] || ln -s /proc/self/fd/2 /dev/stderr
mount -t tmpfs -o mode=0755,size=32M tmpfs /run || fail run
[ "$(uname -r)" = 7.2.6 ] || fail 'kernel mismatch'
grep -q 'valve,steamlink' /sys/firmware/devicetree/base/compatible || fail 'board mismatch'
volume=root-a
seen=0
for word in $(cat /proc/cmdline); do
    case "$word" in
        steamlink.root=*) volume=${word#steamlink.root=}; seen=$((seen + 1));;
    esac
done
[ "$seen" -le 1 ] || fail 'duplicate root selection'
case "$volume" in root-a|root-b) ;; *) fail 'unknown root volume';; esac
modprobe steamlink_marvell_internal_root || fail 'root NAND module'
modprobe steamlink_nand_overlay || fail 'NAND overlay'
[ "$(cat /sys/class/mtd/mtd0/name)" = steamlink-internal-root-ro ] || fail 'MTD identity'
[ "$(cat /sys/class/mtd/mtd0/size)" = 974127104 ] || fail 'MTD capacity'
[ "$(cat /sys/class/mtd/mtd0/erasesize)" = 524288 ] || fail 'MTD erase geometry'
[ "$(cat /sys/class/mtd/mtd0/writesize)" = 4096 ] || fail 'MTD page geometry'
[ "$(cat /sys/class/mtd/mtd0/flags)" = 0x4000 ] || fail 'NAND must be read-only SLC emulation'
[ ! -e /sys/class/ubi/ubi0 ] || fail 'unexpected existing UBI attachment'
ubiattach -m 0 -d 0 -b 20 --disable-fastmap || fail 'UBI attach'
mkdir -p /run/steamlink/ubi /run/steamlink/lower
case "$volume" in root-a) other=root-b;; root-b) other=root-a;; esac
verified=0
for selected in "$volume" "$other"; do
    if mount -t ubifs -o ro "ubi0:$selected" /run/steamlink/ubi; then
        if printf '%s  %s\n' '@ROOT_SHA256@' /run/steamlink/ubi/root.squashfs | sha256sum -c -; then
            verified=1
            break
        fi
        umount /run/steamlink/ubi || fail 'failed-root cleanup'
    fi
done
[ "$verified" = 1 ] || fail 'both root volumes rejected'
printf 'root-sha256 %s\nsystem %s\nvolume %s\n' '@ROOT_SHA256@' '@SYSTEM@' "$selected" >/run/steamlink/root-verified
mount -t squashfs -o loop,ro /run/steamlink/ubi/root.squashfs /run/steamlink/lower || fail 'compressed root'
mount -t tmpfs -o mode=0755,size=128M tmpfs /newroot || fail 'RAM root'
mkdir -p /newroot/nix/store /newroot/nix/var/nix/profiles /newroot/nix/var/nix/gcroots \
    /newroot/etc /newroot/sbin /newroot/root /newroot/tmp /newroot/proc /newroot/sys /newroot/dev /newroot/run /newroot/boot
chmod 1777 /newroot/tmp
chmod 700 /newroot/root
mount --bind /run/steamlink/lower/nix/store /newroot/nix/store || fail 'store bind'
mount -o remount,bind,ro /newroot/nix/store || fail 'immutable store'
cp /run/steamlink/lower/nix-path-registration /newroot/nix-path-registration || fail 'store registration'
cp /run/steamlink/lower/etc/NIXOS /newroot/etc/NIXOS || fail 'NixOS identity'
# The installer may append a private cpio member outside the Nix store. Reuse
# this device's existing SSH host identity in RAM without a writable NAND root.
if [ -e /steamlink-identity ] || [ -L /steamlink-identity ]; then
    [ -d /steamlink-identity ] && [ ! -L /steamlink-identity ] || fail 'host identity directory'
    mkdir -p /newroot/etc/ssh
    for name in ssh_host_ed25519_key ssh_host_ed25519_key.pub; do
        [ -f "/steamlink-identity/$name" ] && [ ! -L "/steamlink-identity/$name" ] || fail 'host identity file'
        cp "/steamlink-identity/$name" "/newroot/etc/ssh/$name" || fail 'host identity copy'
    done
    chmod 600 /newroot/etc/ssh/ssh_host_ed25519_key
    chmod 644 /newroot/etc/ssh/ssh_host_ed25519_key.pub
fi
ln -s '@SYSTEM@' /newroot/nix/var/nix/profiles/system
ln -s /nix/var/nix/profiles/system /newroot/nix/var/nix/gcroots/system
ln -s '@SYSTEM@/init' /newroot/init
ln -s '@SYSTEM@/init' /newroot/sbin/init
[ -x '/newroot@SYSTEM@/init' ] || fail 'stage-2 entry missing'
for name in proc sys dev run; do
    mount --move "/$name" "/newroot/$name" || fail "move $name"
done
exec switch_root /newroot '@SYSTEM@/init'
