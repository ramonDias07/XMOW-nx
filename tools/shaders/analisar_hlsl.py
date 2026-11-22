r"""Estatisticas do HLSL traduzido: leitura manual de vertices (dsVfetch), g_ManualesR0 e ordinais >= 16.
Uso: python -I analisar_hlsl.py <pasta hlsl> <saida.tsv>"""
import re, sys
from pathlib import Path
pasta = Path(sys.argv[1])
rx = re.compile(r"dsVfetch\((\d+),")
rm = re.compile(r"g_ManualesR0 = (\d+)u")
linhas = ["shader\tleitura_manual\tmax_ordinal\tmanuales_r0_mascara\tmanuales_r0_elementos"]
tot = {"vs": 0, "vs_manual": 0, "vs_ord16": 0, "r0": 0, "r0_ord16": 0}
for f in sorted(pasta.glob("vs_*.hlsl")):
    t = f.read_text(errors="replace")
    ks = [int(x) for x in rx.findall(t)]
    m = rm.search(t)
    mask = int(m.group(1)) if m else 0
    tot["vs"] += 1
    if ks: tot["vs_manual"] += 1
    mx = max(ks) if ks else -1
    if mx >= 16: tot["vs_ord16"] += 1
    if mask:
        tot["r0"] += 1
        if mx >= 16: tot["r0_ord16"] += 1
    linhas.append(f"{f.stem}\t{int(bool(ks))}\t{mx}\t{mask:#x}\t{bin(mask).count('1')}")
Path(sys.argv[2]).write_text("\n".join(linhas) + "\n")
print(tot)
