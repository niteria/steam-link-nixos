set -eu
test "$(findmnt --first-only -no FSTYPE --target /)" = ubifs
test "$(findmnt --first-only -no FSTYPE --target /nix/store)" = ubifs
test "$(cat /sys/class/ubi/ubi0/ro_mode)" = 0
test "$(cat /sys/class/ubi/ubi0/bad_peb_count)" = @BAD_BLOCK_COUNT@
test "$(cat /sys/class/ubi/ubi0/reserved_for_bad)" -ge 160
pool=$(cat /sys/class/ubi/ubi0/mtd_num)
test "$(cat /sys/class/mtd/mtd$pool/name)" = steamlink-reclaimed-root
test "$(cat /sys/class/mtd/mtd$pool/size)" = 1443889152
test "$(cat /sys/class/mtd/mtd$pool/flags)" = 0x400
test "$(cat /sys/module/steamlink_reclaim_pool/parameters/write_enable)" = Y
test "$(cat /sys/module/$driver/parameters/writes_faulted)" = N
if [ "$driver" = steamlink_marvell_reclaim ]; then
    test "$(cat /sys/module/$driver/parameters/state_nonce)" = "$(printf '%u' 0x@STATE_NONCE@)"
fi
for path in /sys/class/mtd/mtd*/name; do
    case "$(cat "$path")" in
        steamlink-reclaim-*-ro) test "$(cat "${path%/name}/flags")" = 0x0;;
    esac
done
test -x /run/initramfs/shutdown
