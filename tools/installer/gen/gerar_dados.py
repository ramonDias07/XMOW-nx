r"""Builds the data shipped with xmow_setup.exe (run once here, with python -I; the shipped tool does not need Python).
Reads (read only): S\out\entrada + S\out\entrada.tsv, S\out\extraidos (the .xxx extractor output),
deko-shaders\xex\manifest.tsv, deko-shaders\work\containers_despejos, S\fuente\despejos-teste5\containers_despejos,
re-xex\image.bin (only to check the XEX carve recipe).
Writes I\dist\data\{manifesto.tsv, xex.tsv, variantes.tsv} and I\gen\variantes_relatorio.tsv.
"""
import hashlib, struct, sys
from pathlib import Path
import xxhash

T = Path(__file__).resolve().parents[2]            # ...\trabalho
I = T / "instalador"; S = T / "shaders-g2"; D = T / "deko-shaders"
DATA = I / "dist" / "data"; DATA.mkdir(parents=True, exist_ok=True)
sha = lambda b: hashlib.sha256(b).hexdigest()
x3 = lambda b: "%016X" % xxhash.xxh3_64_intdigest(b)

# 1) manifest from S\out\entrada (checked against entrada.tsv and contenedores)
ent = {}
for l in (S / "out" / "entrada.tsv").read_text(encoding="utf-8").splitlines()[1:]:
    c = l.split("\t"); ent[c[0]] = (c[1], c[2], int(c[4]), c[5])
files = {f.name: f.read_bytes() for f in sorted((S / "out" / "entrada").glob("*.bin"))}
assert set(files) == set(ent), "entrada.tsv != entrada"
for n, d in files.items():
    assert sha(d) == ent[n][3] and len(d) == ent[n][2], n
cont = {f.name: sha(f.read_bytes()) for f in (S / "out" / "contenedores").glob("*.bin")}
assert cont == {n: sha(d) for n, d in files.items()}, "contenedores != entrada"
with open(DATA / "manifesto.tsv", "w", encoding="ascii", newline="\n") as f:
    f.write("# arquivo\ttamanho\tsha256 (16457 containers = S\\out\\entrada of the 0.1.23b run)\n")
    for n in sorted(files):
        f.write(f"{n}\t{len(files[n])}\t{sha(files[n])}\n")
print("manifest", len(files))

# 2) classify
disc = {}                                           # sha -> entrada name, for the .xxx extractor set
for f in (S / "out" / "extraidos").glob("*.bin"):
    disc[sha(f.read_bytes())] = None
by_sha = {sha(d): n for n, d in files.items()}
for h in disc: disc[h] = by_sha[h]
assert len(disc) == 16372 and all(disc.values())
xexsha = {}
for f in (D / "xex").glob("*.bin"):
    xexsha[sha(f.read_bytes())] = f.stem
rest = [n for n, d in files.items() if sha(d) not in disc]
xexn = [n for n in rest if sha(files[n]) in xexsha]
var = [n for n in rest if sha(files[n]) not in xexsha]
print("disc", len(disc), "xex", len(xexn), "variants", len(var))
assert len(xexn) == 18 and len(var) == 67

# 3) XEX recipe: offset of the microcode in the decrypted image + our header bytes
img = (T / "re-xex" / "image.bin").read_bytes()
man = {}
for l in (D / "xex" / "manifest.tsv").read_text().splitlines()[1:]:
    c = l.split("\t"); man[c[0]] = (int(c[2]), int(c[3].split(";")[0], 16))
with open(DATA / "xex.tsv", "w", encoding="ascii", newline="\n") as f:
    f.write("# arquivo\toffset_no_image\tpalavras\tcabecalho_hex (container = cabecalho + image[offset:offset+4*palavras])\n")
    for n in sorted(xexn):
        d = files[n]; stem = xexsha[sha(d)]; words, off = man[stem]
        fis = struct.unpack(">I", d[8:12])[0]; virt = struct.unpack(">I", d[4:8])[0]
        assert fis == 4 * words and d[virt:] == img[off:off + fis], n
        f.write(f"{n}\t{off:#x}\t{words}\t{d[:virt].hex()}\n")

# 4) variants: parent = disc container with the same size and fewest differing u32 words
base = {}
for h, n in disc.items():
    base.setdefault(len(files[n]), []).append(n)
rel = ["arquivo\tmodo\tpai\ttamanho\tpalavras_diferentes\tbytes_dados"]
out = ["# arquivo\ttamanho\tsha256\tmodo\tpai\tdados   (diff: idx=valor hex u32 BE; raw: container hex)"]
for n in sorted(var):
    d = files[n]; best = None
    for p in sorted(base.get(len(d), [])):
        c = files[p]
        k = [i for i in range(0, len(d), 4) if d[i:i + 4] != c[i:i + 4]]
        if best is None or len(k) < len(best[1]):
            best = (p, k)
    if best and len(best[1]) * 13 < len(d) * 2:
        p, k = best
        ops = ";".join("%x=%s" % (i // 4, d[i:i + 4].hex()) for i in k)
        out.append(f"{n}\t{len(d)}\t{sha(d)}\tdiff\t{p}\t{ops}")
        rel.append(f"{n}\tdiff\t{p}\t{len(d)}\t{len(k)}\t{4 * len(k)}")
    else:
        out.append(f"{n}\t{len(d)}\t{sha(d)}\traw\t-\t{d.hex()}")
        rel.append(f"{n}\traw\t{best[0] if best else '-'}\t{len(d)}\t{len(best[1]) if best else '-'}\t{len(d)}")
(DATA / "variantes.tsv").write_text("\n".join(out) + "\n", encoding="ascii", newline="\n")
(I / "gen" / "variantes_relatorio.tsv").write_text("\n".join(rel) + "\n", encoding="ascii")
print("\n".join(l for l in rel if "\traw\t" in l))
print("diff words", sum(int(l.split("\t")[4]) for l in rel[1:] if "\tdiff\t" in l))
