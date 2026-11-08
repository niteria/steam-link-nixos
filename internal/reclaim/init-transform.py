"""After writable/init-transform.py: admit the appended pool and both migration phases."""
def replace(old,new):
    global script
    assert script.count(old)==1,old
    script=script.replace(old,new)

replace('ubidetach -m 0 || hold ubi-detach','ubidetach -d 0 || hold ubi-detach')
replace('if [ -d /sys/module/steamlink_marvell_state ]; then rmmod steamlink_marvell_state || hold writer-unload; fi',
'''if [ -d /sys/module/steamlink_reclaim_pool ]; then rmmod steamlink_reclaim_pool || hold pool-unload; fi
        if [ -d /sys/module/steamlink_marvell_reclaim ]; then rmmod steamlink_marvell_reclaim || hold writer-unload; fi''')
start=script.index('modprobe steamlink_marvell_state write_enable=')
end=script.index('mkdir -p /newroot/etc/ssh /newroot/run',start)
admission=r'''modprobe steamlink_marvell_reclaim write_enable=1 confirm_start=20971520 confirm_blocks=2754 state_nonce=0x@NONCE@ || fail nand-writer
[ "$(cat /sys/module/steamlink_marvell_reclaim/parameters/state_nonce)" = @NONCE_DEC@ ] || fail nonce
modprobe steamlink_reclaim_pool write_enable=1 || fail appended-pool
mtd_number() {
    for path in /sys/class/mtd/mtd*/name; do
        if [ "$(cat "$path")" = "$1" ]; then value=${path%/name}; printf '%s\n' "${value##*/mtd}"; return; fi
    done
    return 1
}
pool=$(mtd_number steamlink-reclaimed-root) || fail pool-name
[ "$(cat /sys/class/mtd/mtd$pool/size)" = 1443889152 ] || fail pool-size
[ "$(cat /sys/class/mtd/mtd$pool/erasesize)" = 524288 ] || fail pool-erase-size
[ "$(cat /sys/class/mtd/mtd$pool/flags)" = 0x400 ] || fail pool-flags
for name in physical boot stock-low identity-boot stock-root metadata-bbt; do
    number=$(mtd_number steamlink-reclaim-$name-ro) || fail protected-name
    [ "$(cat /sys/class/mtd/mtd$number/flags)" = 0x0 ] || fail protected-view
done
# A logical concatenated master needs an explicit budget: default-b20 would
# silently lower the existing whole-chip reserve from160 to54 blocks.
ubiattach -m "$pool" -d 0 -b 60 --disable-fastmap || fail ubi-attach
[ "$(cat /sys/class/ubi/ubi0/ro_mode)" = 0 ] || fail ubi-readonly
[ "$(cat /sys/class/ubi/ubi0/bad_peb_count)" = @BAD_BLOCK_COUNT@ ] || fail bad-block-map
[ "$(cat /sys/class/ubi/ubi0/reserved_for_bad)" -ge 160 ] || fail bad-block-reserve
for spec in 0:root-a:448 2:state:272; do
    id=${spec%%:*}; rest=${spec#*:}; name=${rest%%:*}; count=${rest##*:}
    [ "$(cat /sys/class/ubi/ubi0_$id/name)" = "$name" ] || fail volume-name
    [ "$(cat /sys/class/ubi/ubi0_$id/reserved_ebs)" = "$count" ] || fail volume-size
done
root_volume=
for path in /sys/class/ubi/ubi0_*/name; do
    if [ "$(cat "$path")" = root-b ]; then root_volume=${path%/name}; fi
done
[ -n "$root_volume" ] || fail root-volume
root_lebs=$(cat "$root_volume/reserved_ebs")
case "$root_lebs" in 900|1800) ;; *) fail root-capacity;; esac
[ "$(cat "$root_volume/upd_marker")" = 0 ] || fail incomplete-root-update
mount -t ubifs -o rw,noatime,compr=zstd ubi0:root-b /newroot || fail root-mount
marker=$(cat /newroot/.steamlink-writable-root)
case "$marker" in
    'steamlink-ubifs-root-v1 @SNAPSHOT@ @NONCE@ root-b 900')
        [ "$root_lebs" = 900 ] || fail old-root-cannot-grow;;
    'steamlink-ubifs-root-v1 @SNAPSHOT@ @NONCE@ root-b 1800') ;;
    *) fail root-marker;;
esac
'''.replace('@NONCE@',nonce).replace('@NONCE_DEC@',str(int(nonce,16))).replace('@SNAPSHOT@',snapshot)
bad_blocks = int(os.environ['EXPECTED_BAD_BLOCKS'])
assert 0 <= bad_blocks <= 4096
admission = admission.replace('@BAD_BLOCK_COUNT@', str(bad_blocks))
script=script[:start]+admission+script[end:]
