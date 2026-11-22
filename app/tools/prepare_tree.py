#!/usr/bin/env python3
"""Mirror the generated code (out/codegen/generated/default/) for the Switch build, WITHOUT symbolic links.

Local copy of gears2-nx gow2/tools/prepare_tree.py (which links every file and fails on Windows with WinError 1314
without developer mode / admin). Here each file is hard-linked (same volume, no privilege needed) and, if that is not
possible, copied. A file already there with the same size and modification time is left alone, so a re-run does not
touch the build timestamps. Only the PCH is replaced by the Switch one (make_nx_pch.py).

Usage: prepare_tree.py <generated/default> <out_dir>
"""
import os, shutil, subprocess, sys
from pathlib import Path

src = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2]).resolve()
here = Path(__file__).resolve().parent
PCH = "xmow_pch.h"

n = linked = copied = kept = 0
for root, dirs, files in os.walk(src):
    rel = Path(root).relative_to(src)
    (out / rel).mkdir(parents=True, exist_ok=True)
    for f in files:
        if rel == Path(".") and f == PCH:
            continue
        dest = out / rel / f
        origin = Path(root) / f
        if dest.exists():
            a, b = origin.stat(), dest.stat()
            if a.st_size == b.st_size and int(a.st_mtime) == int(b.st_mtime):
                kept += 1
                continue
            dest.unlink()
        try:
            os.link(origin, dest)
            linked += 1
        except OSError:
            shutil.copy2(origin, dest)
            copied += 1
        n += 1
subprocess.check_call([sys.executable, "-I", str(here / "make_nx_pch.py"), str(src / PCH), str(out / PCH)])
print(f"{n} files in {out} ({linked} hard links, {copied} copies, {kept} already there)")
