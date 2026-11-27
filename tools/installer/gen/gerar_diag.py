r"""Hash lists (diagnostic only) of the 0.1.23b intermediate outputs: S\out\hlsl\*.hlsl and S\out\spirv\*.spv."""
import hashlib
from pathlib import Path
T = Path(__file__).resolve().parents[2]; S = T / "shaders-g2" / "out"; D = T / "instalador" / "dist" / "data"
for pasta, ext, nome in (("hlsl", "*.hlsl", "diag_hlsl.tsv"), ("spirv", "*.spv", "diag_spirv.tsv")):
    fs = sorted((S / pasta).glob(ext))
    with open(D / nome, "w", encoding="ascii", newline="\n") as f:
        f.write(f"# {pasta}: arquivo\ttamanho\tsha256 (S\\out\\{pasta} of the 0.1.23b run; diagnostic)\n")
        for p in fs:
            b = p.read_bytes(); f.write(f"{p.name}\t{len(b)}\t{hashlib.sha256(b).hexdigest()}\n")
    print(nome, len(fs))
