#!/usr/bin/env python3
"""Post-codegen game patches for X-Men Origins: Wolverine (default.xex SHA-256 2F987F7B...5378, retail, no TU).

Each patched site asks XmowGamePatch(id) (app/src/xmow_game_patches.cpp) whether it is on, so a patch can be
turned off from xmow.toml without a rebuild. Idempotent. Modelled on singularity-nx's patch_singularity.py
(panalexeu/singularity-nx, GPL-3).

  id 1  No predicated tiling: the renderer init (sub_82374410) stores 1 into GUseTilingMode (0x836088E8) with
        `stw r11,-30488(r6)` (r6 = lis -31905); the patch stores 0. Found from the TILING console command (it reads
        the strings "TILING"/"TILINGMODE" at 0x820BEC44/54 and flips that variable). The game's other stores to it
        only write 0 or put back a saved value.

Usage: patch_xmow_jogo.py generated/default/*.cpp
"""
import glob
import sys

MARK = "XMOW GAMEPATCH"
DECL = f'extern "C" int XmowGamePatch(int id);  // {MARK}\n'

PATCHES = [
    # (id, original lines, patched lines)
    (1,
     "\t// stw r11,-30488(r6)\n\tREX_STORE_U32(ctx.r6.u32 + -30488, ctx.r11.u32);\n",
     f"\t// stw r11,-30488(r6)  -- {MARK} 1: GUseTilingMode = FALSE\n"
     "\tREX_STORE_U32(ctx.r6.u32 + -30488, XmowGamePatch(1) ? 0u : ctx.r11.u32);\n"),
]


def main(args):
    files = [f for a in args for f in sorted(glob.glob(a))]
    if not files:
        print("no files")
        return 1
    found = {pid: [] for pid, _, _ in PATCHES}
    for path in files:
        with open(path, encoding="utf-8", newline="") as f:
            text = f.read()
        new = text
        nl = "\r\n" if "\r\n" in text else "\n"  # the codegen writes CRLF on Windows
        for pid, old, patched in PATCHES:
            old, patched = old.replace("\n", nl), patched.replace("\n", nl)
            n = new.count(old)
            if n:
                found[pid].append((path, n))
                new = new.replace(old, patched)
            elif patched in new:
                found[pid].append((path, 0))  # already patched
        if new != text:
            if DECL.replace("\n", nl) not in new:
                new = DECL.replace("\n", nl) + new
            with open(path, "w", encoding="utf-8", newline="") as f:
                f.write(new)
    ok = True
    for pid, where in found.items():
        total = sum(n for _, n in where)
        if not where:
            print(f"patch {pid}: site NOT found (different default.xex?)")
            ok = False
        elif total > 1:
            print(f"patch {pid}: {total} sites, expected 1: {where}")
            ok = False
        else:
            print(f"patch {pid}: {'applied' if total else 'already applied'} in {where[0][0]}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
