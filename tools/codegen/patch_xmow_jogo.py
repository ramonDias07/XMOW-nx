#!/usr/bin/env python3
"""Post-codegen game patches for X-Men Origins: Wolverine (default.xex SHA-256 2F987F7B...5378, retail, no TU).

Each patched site asks XmowGamePatch(id) (xmow-g2/src/xmow_game_patches.cpp) whether it is on, so a patch can be
turned off from xmow.toml without a rebuild. Idempotent. Modelled on singularity-nx's patch_singularity.py
(panalexeu/singularity-nx, GPL-3).

  id 1  No predicated tiling: the renderer init (sub_82374410) stores 1 into GUseTilingMode (0x836088E8) with
        `stw r11,-30488(r6)` (r6 = lis -31905); the patch stores 0. Found from the TILING console command (it reads
        the strings "TILING"/"TILINGMODE" at 0x820BEC44/54 and flips that variable). The game's other stores to it
        only write 0 or put back a saved value.
  id 2  (0.1.28) View distance: in InitViews (sub_82606A08, xmow_recomp.158.cpp) the per-primitive distance cull
        `lfs f5,44(r24)` (the compact entry's MaxDrawDistance) / `fmuls f4,f5,f5` / `fcmpu cr6,f31,f4` /
        `bgt cr6,0x82608104` (pesquisa-fps-picos.md 1d) compares the squared distance f31 against f4 * g_xmow_corte_k2
        (xmow_game_patches.cpp: (xmow_distancia_corte / 100)^2, 1.0 by default, so the compare is bit-exact), only for
        0 < MaxDrawDistance < 262144 (0, negative, WORLD_MAX 524288 and FLT_MAX stay as they are). Only the compare
        changes: f4 and f31 keep their values (the static mesh LOD and decal tests read f31 later). A regex, so the raw
        codegen form (ctx.fN) and the form after patch_local_regs (L_fN) both match; patch_local_regs renames the
        ctx.f5 the patch adds with the rest of the function.
  id 3  (0.1.28) Mesh detail: in the same function, the static mesh element loop (loc_826078F4) draws an element (one
        LOD's section) when MinDrawDistanceSq (+224) <= LODDistanceFactor^2 * DistSq (f0) < MaxDrawDistanceSq (+228):
        `fmuls f0,f11,f31` / `fcmpu cr6,f0,f10` / `fcmpu cr6,f0,f11`. Both compares take f0 * g_xmow_lod_k2
        (xmow_game_patches.cpp: (xmow_detalhe_malha / 100)^2, 1.0 by default = bit-exact): every static mesh gets the
        LOD it would get from that much farther away. The LOD ranges stay contiguous, so one LOD is still picked.
  id 4  (0.1.29) Small object cull: in the same function, at loc_826077E0 (just before the frustum test
        `bl 0x82896378` with View+736 r27, the bounds origin r21 and the sphere radius at compact+40 r31), while
        g_xmow_pequeno_k2 > 0 the patch calls XmowCortePequeno(radius, origin xyz, View+720/724/728/732) and goes to
        the cull branch 0x82608104 when it answers 1 (bounding sphere radius < sqrt(k2) * distance, perspective views
        only). With the option off (k2 = 0) the added test reads one global and the game's code runs as before.

Usage: patch_xmow_jogo.py generated/default/*.cpp
"""
import glob
import re
import sys

MARK = "XMOW GAMEPATCH"
DECL = f'extern "C" int XmowGamePatch(int id);  // {MARK}\n'
DECL2 = f'extern "C" double g_xmow_corte_k2;  // {MARK} 2\n'
MARK2 = f"{MARK} 2: view distance"
DECL3 = f'extern "C" double g_xmow_lod_k2;  // {MARK} 3\n'
MARK3 = f"{MARK} 3: mesh detail"
DECL4 = (f'extern "C" double g_xmow_pequeno_k2;  // {MARK} 4\n'
         f'extern "C" int XmowCortePequeno(unsigned int radio, unsigned int ox, unsigned int oy, unsigned int oz, unsigned int vx, '
         f'unsigned int vy, unsigned int vz, unsigned int vw);  // {MARK} 4\n')
MARK4 = f"{MARK} 4: small object cull"

PATCHES = [
    # (id, original lines, patched lines)
    (1,
     "\t// stw r11,-30488(r6)\n\tREX_STORE_U32(ctx.r6.u32 + -30488, ctx.r11.u32);\n",
     f"\t// stw r11,-30488(r6)  -- {MARK} 1: GUseTilingMode = FALSE\n"
     "\tREX_STORE_U32(ctx.r6.u32 + -30488, XmowGamePatch(1) ? 0u : ctx.r11.u32);\n"),
    # lote C (gerador do fork do LoS1-nx com dform_disp_split): o mesmo stw, registrador e deslocamento separados.
    # found[] e por id, entao as duas formas do id 1 contam juntas (continua exigindo exatamente 1 sitio).
    (1,
     "\t// stw r11,-30488(r6)\n\tREX_STORE_D_U32(ctx.r6.u32, -30488, ctx.r11.u32);\n",
     f"\t// stw r11,-30488(r6)  -- {MARK} 1: GUseTilingMode = FALSE\n"
     "\tREX_STORE_D_U32(ctx.r6.u32, -30488, XmowGamePatch(1) ? 0u : ctx.r11.u32);\n"),
]

# id 2: the compare after `lfs f5,44(r24)` ... `fmuls f4,f5,f5` ... `fcmpu cr6,f31,f4`, followed by the cull branch.
RE2 = re.compile(
    r"(\t// lfs f5,44\(r24\)\r?\n(?:[^\n]*\n){1,3}?\t// fmuls f4,f5,f5\r?\n(?:[^\n]*\n){1,8}?"
    r"\t// fcmpu cr6,f31,f4\r?\n\t)((?:ctx\.)?cr6)\.compare\(((?:ctx\.|L_)f31\.f64), ((ctx\.|L_)f4\.f64)\);"
    r"(\r?\n\t// bgt cr6,0x82608104)")


def patch2(text):
    """Returns (new text, sites patched now, already patched)."""
    if MARK2 in text:
        return text, 0, True

    def sub(m):
        f5 = f"{m.group(5)}f5.f64"
        return (f"{m.group(1)}{m.group(2)}.compare({m.group(3)}, ({f5} > 0.0 && {f5} < 262144.0) ? "
                f"{m.group(4)} * g_xmow_corte_k2 : {m.group(4)});  // {MARK2}{m.group(6)}")

    new, n = RE2.subn(sub, text)
    return new, n, False


# id 3: the two LOD range compares of the static mesh element loop (loc_826078F4).
RE3 = re.compile(
    r"(\t// fmuls f0,f11,f31\r?\n\t(?:ctx\.|L_)f0\.f64 = double\(float\((?:ctx\.|L_)f11\.f64 \* (?:ctx\.|L_)f31\.f64\)\);"
    r"\r?\n(?:[^\n]*\n){1,4}?\t// fcmpu cr6,f0,f10\r?\n\t)((?:ctx\.)?cr6)\.compare\(((?:ctx\.|L_)f0\.f64), "
    r"((?:ctx\.|L_)f10\.f64)\);(\r?\n\t// blt cr6,0x82607a3c\r?\n(?:[^\n]*\n){1,5}?\t// fcmpu cr6,f0,f11\r?\n\t)"
    r"((?:ctx\.)?cr6)\.compare\(((?:ctx\.|L_)f0\.f64), ((?:ctx\.|L_)f11\.f64)\);")


def patch3(text):
    """Returns (new text, sites patched now, already patched)."""
    if MARK3 in text:
        return text, 0, True

    def sub(m):
        return (f"{m.group(1)}{m.group(2)}.compare({m.group(3)} * g_xmow_lod_k2, {m.group(4)});  // {MARK3}"
                f"{m.group(5)}{m.group(6)}.compare({m.group(7)} * g_xmow_lod_k2, {m.group(8)});  // {MARK3}")

    new, n = RE3.subn(sub, text)
    return new, n, False


# id 4: the label before the frustum test of the primitive loop, followed by `addi r31,r24,40`.
RE4 = re.compile(r"(\nloc_826077E0:\r?\n)(\t// addi r31,r24,40\r?\n\t(ctx\.|L_)r31\.s64 = (?:ctx\.|L_)r24\.s64 \+ 40;)")


def patch4(text):
    """Returns (new text, sites patched now, already patched)."""
    if MARK4 in text:
        return text, 0, True

    def sub(m):
        p = m.group(3)
        ld = lambda reg, off: f"REX_LOAD_D_U32({p}{reg}.u32, {off})"
        return (f"{m.group(1)}\t// {MARK4}: a bounding sphere too small for its distance goes to the cull branch{NL}"
                f"\tif (g_xmow_pequeno_k2 > 0.0 && XmowCortePequeno({ld('r24', 40)}, {ld('r21', 0)}, {ld('r21', 4)}, "
                f"{ld('r21', 8)}, {ld('r29', 720)}, {ld('r29', 724)}, {ld('r29', 728)}, {ld('r29', 732)})) "
                f"goto loc_82608104;{NL}{m.group(2)}")

    new, n = RE4.subn(sub, text)
    return new, n, False


NL = "\n"


def main(args):
    files = [f for a in args for f in sorted(glob.glob(a))]
    if not files:
        print("no files")
        return 1
    found = {pid: [] for pid, _, _ in PATCHES}
    found[2] = []
    found[3] = []
    found[4] = []
    for path in files:
        with open(path, encoding="utf-8", newline="") as f:
            text = f.read()
        new = text
        nl = "\r\n" if "\r\n" in text else "\n"  # the codegen writes CRLF on Windows
        p1 = False
        for pid, old, patched in PATCHES:
            old, patched = old.replace("\n", nl), patched.replace("\n", nl)
            n = new.count(old)
            if n:
                found[pid].append((path, n))
                new = new.replace(old, patched)
                p1 = True
            elif patched in new:
                found[pid].append((path, 0))  # already patched
        p2 = False
        if "fcmpu cr6,f31,f4" in new or MARK2 in new:
            new, n2, ya = patch2(new)
            if n2 or ya:
                found[2].append((path, n2))
            p2 = bool(n2)
        p3 = False
        if "fcmpu cr6,f0,f10" in new or MARK3 in new:
            new, n3, ya3 = patch3(new)
            if n3 or ya3:
                found[3].append((path, n3))
            p3 = bool(n3)
        p4 = False
        if "loc_826077E0:" in new or MARK4 in new:
            global NL
            NL = nl
            new, n4, ya4 = patch4(new)
            if n4 or ya4:
                found[4].append((path, n4))
            p4 = bool(n4)
        if new != text:
            if p4 and DECL4.replace("\n", nl) not in new:
                new = DECL4.replace("\n", nl) + new
            if p3 and DECL3.replace("\n", nl) not in new:
                new = DECL3.replace("\n", nl) + new
            if p2 and DECL2.replace("\n", nl) not in new:
                new = DECL2.replace("\n", nl) + new
            if p1 and DECL.replace("\n", nl) not in new:
                new = DECL.replace("\n", nl) + new
            # In place (same file, same inode): the build's hard-linked mirror (bld-g2/generated_nx) sees it too.
            with open(path, "r+", encoding="utf-8", newline="") as f:
                f.seek(0)
                f.write(new)
                f.truncate()
    ok = True
    for pid, where in found.items():
        total = sum(n for _, n in where)
        if not where:
            print(f"patch {pid}: site NOT found (different default.xex?)")
            ok = False
        elif total > 1 or len(where) > 1:
            print(f"patch {pid}: {total} sites, expected 1: {where}")
            ok = False
        else:
            print(f"patch {pid}: {'applied' if total else 'already applied'} in {where[0][0]}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
