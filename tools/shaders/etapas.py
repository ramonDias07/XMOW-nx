r"""Etapas do pipeline de shaders do gears2-nx, portado para Windows (XMOW).
Uso: python -I etapas.py <etapa> [...]   (etapas: hlsl spirv empaquetar)
Tudo relativo a XMOW_SHADERS_OUT (bin/, out/, logs/). DXC e SPIRV_VAL = executaveis (padrao: no PATH).
JOBS (padrao 10) = processos paralelos.
"""
import os, re, subprocess, sys, shutil, time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

RAIZ = Path(os.environ["XMOW_SHADERS_OUT"]).resolve()
FONTES = Path(__file__).resolve().parent / "src"
DXC = Path(os.environ.get("DXC", "dxc"))
SPVVAL = Path(os.environ.get("SPIRV_VAL", "spirv-val"))
BIN = RAIZ / "bin"
OUT = RAIZ / "out"
LOGS = RAIZ / "logs"
JOBS = int(os.environ.get("JOBS", "10"))
CF = subprocess.CREATE_NO_WINDOW


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, errors="replace", creationflags=CF, **kw)


def etapa_hlsl():
    entrada = OUT / "entrada"
    tmp = OUT / "hlsl_partes"
    shutil.rmtree(tmp, ignore_errors=True)
    shutil.rmtree(OUT / "hlsl", ignore_errors=True)
    arquivos = sorted(entrada.glob("*.bin"))
    n = JOBS
    for k in range(n):
        (tmp / f"in{k:02d}").mkdir(parents=True)
    for i, f in enumerate(arquivos):
        os.link(f, tmp / f"in{i % n:02d}" / f.name)
    comum = FONTES / "XenosRecomp" / "shader_common.h"

    def um(k):
        r = run([str(BIN / "ds_hlsl.exe"), str(tmp / f"in{k:02d}"), str(tmp / f"out{k:02d}"), str(comum), "--saltos-lineales"])
        (LOGS / f"hlsl_{k:02d}.log").write_text(r.stdout + r.stderr, encoding="utf-8")
        return k, r.returncode
    LOGS.mkdir(exist_ok=True)
    t0 = time.time()
    with ThreadPoolExecutor(n) as ex:
        res = list(ex.map(um, range(n)))
    (OUT / "hlsl").mkdir()
    total = 0
    for k in range(n):
        d = tmp / f"out{k:02d}"
        if d.is_dir():
            for f in d.iterdir():
                os.replace(f, OUT / "hlsl" / f.name)
                total += 1
    # falhas de traducao: linhas dos logs que nao sao o resumo
    falhas = []
    for k in range(n):
        for l in (LOGS / f"hlsl_{k:02d}.log").read_text(encoding="utf-8").splitlines():
            if l.startswith("  ") and ("rechazada" in l or "firma" in l or "no produjo" in l or "ilegible" in l):
                falhas.append(l.strip())
    (OUT / "traducao_falhas.txt").write_text("\n".join(falhas) + "\n", encoding="utf-8")
    shutil.rmtree(tmp, ignore_errors=True)
    print(f"hlsl: {total} traduzidos de {len(arquivos)}; {len(falhas)} falhas; {time.time()-t0:.0f}s; codigos {[c for _, c in res]}")


def classe_dxc(texto):
    for l in texto.splitlines():
        m = re.search(r"error: (.*)", l)
        if m:
            s = re.sub(r"'[^']*'", "'X'", m.group(1))
            s = re.sub(r"\d+", "N", s)
            return s[:100]
    return "(sem mensagem)"


def etapa_spirv():
    hlsl = OUT / "hlsl"
    spv = OUT / "spirv"
    err = OUT / "err"
    for d in (spv, err):
        shutil.rmtree(d, ignore_errors=True)
        d.mkdir(parents=True)
    arquivos = sorted(hlsl.glob("*.hlsl"))

    def um(f):
        base = f.stem
        tipo = "ps_6_6" if base.startswith("ps_") else "vs_6_6"
        dst = spv / (base + ".spv")
        try:
            r = run([str(DXC), "-spirv", "-T", tipo, "-E", "main", "-HV", "2021", "-fspv-target-env=vulkan1.2",
                     "-fvk-use-dx-layout", "-Werror=parameter-usage", "-Fo", str(dst), str(f)], timeout=900)
        except subprocess.TimeoutExpired:
            dst.unlink(missing_ok=True)
            (err / (base + ".dxc")).write_text("TIMEOUT", encoding="utf-8")
            return base, "DXC", "timeout 900 s"
        if r.returncode != 0 or not dst.exists():
            dst.unlink(missing_ok=True)
            txt = r.stdout + r.stderr
            (err / (base + ".dxc")).write_text(txt, encoding="utf-8")
            return base, "DXC", classe_dxc(txt)
        v = run([str(SPVVAL), "--target-env", "vulkan1.2", "--scalar-block-layout", str(dst)])
        if v.returncode != 0:
            dst.unlink(missing_ok=True)
            txt = v.stdout + v.stderr
            (err / (base + ".val")).write_text(txt, encoding="utf-8")
            return base, "VAL", classe_dxc(txt)
        return base, "OK", ""
    t0 = time.time()
    with ThreadPoolExecutor(JOBS) as ex:
        res = list(ex.map(um, arquivos))
    ok = sum(1 for r in res if r[1] == "OK")
    with open(OUT / "spirv.log", "w", encoding="utf-8") as f:
        for b, s, c in res:
            f.write(f"{b} {s}\n")
    with open(OUT / "spirv_falhas.tsv", "w", encoding="utf-8") as f:
        f.write("shader\tetapa\tclasse\n")
        for b, s, c in res:
            if s != "OK":
                f.write(f"{b}\t{s}\t{c}\n")
    print(f"spirv: {ok} OK, {len(res)-ok} falhas de {len(res)}; {time.time()-t0:.0f}s")


def etapa_empaquetar():
    cont = OUT / "contenedores"
    shutil.rmtree(cont, ignore_errors=True)
    cont.mkdir()
    for s in sorted((OUT / "spirv").glob("*.spv")):
        os.link(OUT / "entrada" / (s.stem + ".bin"), cont / (s.stem + ".bin"))
    dest = OUT / "xmow_shaders.dslib"
    dest.unlink(missing_ok=True)
    r = run([str(BIN / "ds_empaquetar.exe"), str(cont), str(OUT / "spirv"), str(dest)])
    print(r.stdout.strip(), r.stderr.strip())
    if r.returncode:
        sys.exit(1)


if __name__ == "__main__":
    for e in sys.argv[1:]:
        globals()["etapa_" + e]()
