r"""Compara os containers extraidos pelo extrator adaptado (xmow_extract_shaders) com os do pipeline antigo.
Uso: python -I comparar_extracao.py <extraidos> <deko-shaders>"""
import hashlib, sys
from pathlib import Path
ext = Path(sys.argv[1]); base = Path(sys.argv[2])
def conj(p):
    return {hashlib.sha256(f.read_bytes()).hexdigest() for f in p.glob("*.bin")}
a = conj(ext); b = conj(base / "microcode"); c = conj(base / "work" / "containers")
print(f"extrator adaptado: {len(a)} | microcode antigo (scanner xmow_ue3_scan): {len(b)} | work/containers: {len(c)}")
print(f"extrator e microcode: iguais={len(a & b)}, so no extrator={len(a - b)}, so no antigo={len(b - a)}")
print(f"work/containers fora do extrator: {len(c - a)} (xex/variantes dos despejos sao os esperados)")
