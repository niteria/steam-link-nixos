#!/usr/bin/env python3
"""Copy the exact ARM ELF dependency graph and explicit module dependencies."""
import json
import os
from pathlib import Path
import shutil
import subprocess

tree = Path("tree")
paths = [Path(p) for p in Path(os.environ["CLOSURE_PATHS"]).read_text().splitlines()]
copied = set()
def query(option, path):
    result = subprocess.run(["patchelf", option, str(path)], capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else ""
def materialize(path):
    path = Path(path)
    if str(path) in copied:
        return
    copied.add(str(path))
    target = tree / str(path).lstrip("/")
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(path, target)
    shutil.copymode(path, target)
    interpreter = query("--print-interpreter", path)
    if interpreter:
        materialize(interpreter)
    search = [Path(p.replace("$ORIGIN", str(path.parent))) for p in query("--print-rpath", path).split(":") if p]
    for name in query("--print-needed", path).splitlines():
        candidates = [p / name for p in search if (p / name).is_file()]
        if not candidates:
            candidates = [p for root in paths for p in root.rglob(name) if p.is_file()]
            assert len({p.resolve() for p in candidates}) == 1, (path, name, candidates)
        assert candidates, (path, name)
        materialize(candidates[0])
materialize(os.environ["BUSYBOX"])
materialize(os.environ["UBIATTACH"])
if os.environ.get("UBIDETACH"):
    materialize(os.environ["UBIDETACH"])

modules = Path(os.environ["MODULES"])
dependencies = {}
for line in (modules / "modules.dep").read_text().splitlines():
    name, values = line.split(":", 1)
    dependencies[name] = values.split()
selected = set()
def module(name):
    if name in selected:
        return
    selected.add(name)
    for dependency in dependencies[name]:
        module(dependency)
    target = tree / "lib/modules/7.2.6" / name
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(modules / name, target)
for basename in [os.environ.get("ROOT_MODULE", "steamlink_marvell_internal_root.ko"), "steamlink_nand_overlay.ko", *os.environ.get("EXTRA_MODULES", "").split()]:
    matches = [name for name in dependencies if Path(name).name == basename]
    assert len(matches) == 1, matches
    module(matches[0])
base = tree / "lib/modules/7.2.6"
(base / "modules.dep").write_text("".join(name + ": " + " ".join(dependencies[name]) + "\n" for name in sorted(selected)))
(base / "modules.alias").write_text("")
if (modules / "modules.builtin").exists():
    shutil.copyfile(modules / "modules.builtin", base / "modules.builtin")
report = {"elf_files": sorted(copied), "modules": sorted(selected)}
Path(os.environ["DEPENDENCY_REPORT"]).write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report, indent=2))
