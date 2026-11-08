#!/usr/bin/env python3
"""Check AV clock phandles against the pinned kernel's actual binding header."""
import argparse
import hashlib
import json
import re
import subprocess
import tarfile
from pathlib import Path

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("--kernel-source",type=Path,required=True)
parser.add_argument("--dtb",type=Path,required=True)
parser.add_argument("--output",type=Path,required=True)
args=parser.parse_args()
with tarfile.open(args.kernel_source,"r:xz") as source:
    member=next(m for m in source if m.name.endswith("/include/dt-bindings/clock/berlin2.h"))
    header=source.extractfile(member).read()
ids={name:int(value) for name,value in re.findall(rb'#define\s+(CLKID_\w+)\s+(\d+)',header)}
names=["DRMFIGO","GFX","ZSP","PCUBE","VSCOPE","VPP","APP","AUDIO0","AUDIO2","AUDIO3","AUDIO1","VIP","GC360","AUDIOHD","VIDEO0","VIDEO1","VIDEO2","CFG"]
def cells(node,prop):
    return [int(x,16) for x in subprocess.check_output(["fdtget","-t","x",str(args.dtb),node,prop],text=True).split()]
phandle=cells("/soc@f7000000/chip-control@ea0000/clock","phandle")[0]
expected=[value for name in names for value in (phandle,ids[("CLKID_"+name).encode()])]
assert cells("/steamlink-pe","clocks")==expected
assert len(expected)==36
status=subprocess.check_output(["fdtget",str(args.dtb),"/soc@f7000000/apb@e80000/i2c@1400","status"],text=True).strip()
assert status=="disabled","I2C0 must activate only after the SSH checkpoint"
report={"status":"passed","actual_kernel_binding_sha256":hashlib.sha256(header).hexdigest(),"clock_ids":{name:ids[("CLKID_"+name).encode()] for name in names},"i2c0_boot_status":status}
args.output.write_text(json.dumps(report,indent=2)+"\n")
