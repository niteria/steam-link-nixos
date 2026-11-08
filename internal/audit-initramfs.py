#!/usr/bin/env python3
"""Audit a newc gzip archive without mounts or target-device access."""
import gzip
import json
import os
from pathlib import Path
import struct

data = gzip.decompress(Path(os.environ["ARCHIVE"]).read_bytes())
position = 0
files = {}
hardlinks = {}
while True:
    assert data[position:position + 6] == b"070701"
    fields = [int(data[position + 6 + i * 8:position + 14 + i * 8], 16) for i in range(13)]
    size, namesize = fields[6], fields[11]
    start = position + 110
    name = data[start:start + namesize - 1].decode()
    start = (start + namesize + 3) & ~3
    content = data[start:start + size]
    position = (start + size + 3) & ~3
    if name == "TRAILER!!!":
        break
    assert not name.startswith("/") and ".." not in Path(name).parts
    assert fields[2:4] == [0, 0]
    assert name not in files
    files[name] = (fields[1], content)
    if fields[1] & 0o170000 == 0o100000 and fields[4] > 1:
        key = (fields[7], fields[8], fields[0])
        hardlinks.setdefault(key, []).append(name)
for group in hardlinks.values():
    bodies = {files[name][1] for name in group if files[name][1]}
    assert len(bodies) <= 1
    content = next(iter(bodies), b"")
    for name in group:
        files[name] = (files[name][0], content)
assert files["init"][0] & 0o111
assert b"@ROOT_SHA256@" not in files["init"][1]
for name, (mode, content) in files.items():
    source = Path("tree") / name
    if mode & 0o170000 == 0o100000:
        assert content == source.read_bytes(), name
    elif mode & 0o170000 == 0o120000:
        assert content == os.readlink(source).encode(), name
for executable in ("bin/sh", "bin/ubiattach"):
    target = files[executable][1].decode().lstrip("/")
    mode, elf = files[target]
    if mode & 0o170000 == 0o120000:
        target = str(Path(target).parent / elf.decode())
        mode, elf = files[target]
    assert elf[:4] == b"\x7fELF" and elf[4] == 1 and struct.unpack_from("<H", elf, 18)[0] == 40
for module in (os.environ.get("ROOT_MODULE", "steamlink_marvell_internal_root.ko"), "steamlink_nand_overlay.ko"):
    assert any(name.endswith("/" + module) for name in files)
config = Path(os.environ["KCONFIG"]).read_text()
for option in ("CONFIG_BLK_DEV_INITRD=y", "CONFIG_RD_GZIP=y", "CONFIG_MTD_UBI=y", "CONFIG_UBIFS_FS=y", "CONFIG_BLK_DEV_LOOP=y", "CONFIG_SQUASHFS_ZLIB=y"):
    assert option in config, option
report = {"status": "archive-audit-passed", "members": len(files), "uncompressed_bytes": len(data), "compressed_bytes": Path(os.environ["ARCHIVE"]).stat().st_size}
Path(os.environ["ARCHIVE"]).with_name("verification.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report))
