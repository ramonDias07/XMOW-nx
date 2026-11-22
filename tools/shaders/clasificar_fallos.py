r"""Clasifica las variantes DKSH fallidas (out\dksh.tsv) por clase de error de uam / conversion.
Uso: python -I clasificar_fallos.py <dksh.tsv> <vs_leitura_manual.tsv> <saida.txt>"""
import re, sys, collections
filas = [l.rstrip("\n").split("\t") for l in open(sys.argv[1], encoding="utf-8")][1:]
manual = {l.split("\t")[0]: l.rstrip("\n").split("\t") for l in open(sys.argv[2], encoding="utf-8")}
falhas = [f for f in filas if f[4] != "ok"]
clases = collections.Counter(); shaders = collections.defaultdict(set); ejemplo = {}
for f in falhas:
    motivo = f[9]
    errores = re.findall(r"error: (array index must be < \d+|[^0-9\[]+)", motivo)
    if f[4] != "fallo":
        c = f[4] + ": " + motivo[:60]
    elif motivo.startswith("conversion:"):
        c = "conversion: " + re.sub(r"\d+", "N", motivo[:80])
    elif errores:
        c = "uam: " + re.sub(r"\s+", " ", errores[0]).strip()
    else:
        c = "uam sin 'error:' (" + motivo[:70] + ")"
    clases[c] += 1; shaders[c].add(f[0]); ejemplo.setdefault(c, f[0])
out = []
out.append(f"variantes fallidas: {len(falhas)} de {len(filas)}; shaders afectados: {len({f[0] for f in falhas})}")
for c, n in clases.most_common():
    out.append(f"{n:6d} variantes, {len(shaders[c]):5d} shaders  {c}  (ej. {ejemplo[c]})")
afect = {f[0] for f in falhas}
vs = {s for s in afect if s.startswith("vs_")}
o16 = {s for s in vs if s in manual and int(manual[s][2]) >= 16}
out.append(f"VS afectados {len(vs)}; con ordinal dsVfetch >= 16: {len(o16)}; sin ello: {len(vs - o16)}")
out.append("PS afectados: %d" % len({s for s in afect if s.startswith('ps_')}))
# shaders sin ningun DKSH
por = collections.defaultdict(lambda: [0, 0])
for f in filas:
    por[f[0]][0] += 1; por[f[0]][1] += f[4] == "ok"
sin = [s for s, (n, ok) in por.items() if ok == 0]
out.append(f"shaders sin ninguna variante compilada: {len(sin)} (VS {sum(s.startswith('vs_') for s in sin)}, PS {sum(s.startswith('ps_') for s in sin)})")
open(sys.argv[3], "w", encoding="utf-8").write("\n".join(out) + "\n")
print("\n".join(out))
