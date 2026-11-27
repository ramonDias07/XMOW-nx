r"""data\claves_vs.txt: the 2,355 proven folded VS keys (0.1.33-shaders\out\claves_probadas.txt) in the input format
of ds_vs_plegado: "<container name> <specialization without 0x00100000> <output mask>" (names from the informe_*.tsv
of out\dksh-plegado). Only names and integers."""
from pathlib import Path
T = Path(__file__).resolve().parents[2]; O = T / "0.1.33-shaders" / "out"; D = T / "instalador" / "dist" / "data"
nome = {}
for f in sorted((O / "dksh-plegado").glob("informe_*.tsv")):
    for l in f.read_text(encoding="utf-8", errors="replace").splitlines():
        c = l.split("\t"); nome.setdefault(int(c[1], 16), set()).add(c[0])
lin = []
for l in (O / "claves_probadas.txt").read_text().splitlines():
    if l.startswith("#") or not l.strip(): continue
    h, e, v = (int(x, 16) for x in l.split())
    assert e & 0x00100000
    lin.append((sorted(nome[h])[0], e & ~0x00100000, v))
lin.sort()
with open(D / "claves_vs.txt", "w", encoding="ascii", newline="\n") as f:
    f.write("# container specialization output_mask: the 2,355 proven folded VS keys of shadercache_dksh_vs.bin (0.1.33)\n")
    for n, e, v in lin: f.write(f"{n} {e:08X} {v:08X}\n")
print(len(lin), "keys,", len({n for n, _, _ in lin}), "shaders; fingerprints with >1 name:", sum(1 for s in nome.values() if len(s) > 1))
