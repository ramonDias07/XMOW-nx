r"""Reune os containers do XMOW numa pasta unica para o pipeline do gears2 (ds_hlsl / DXC / ds_empaquetar).

Fontes (somente leitura): a saida do xmow_extract_shaders (pacotes .xxx do disco) e, opcionalmente, pastas extras
com containers capturados no console (shaders_dump/ ao lado do NRO: shaders do XEX e variantes alteradas pelo jogo).
Saida: <saida>/ps_<nome>.bin / vs_<nome>.bin (o build_library do gears2 escolhe o perfil DXC pelo prefixo),
sem repeticao de conteudo, e <saida>/../entrada.tsv com origem e validacao do cabecalho (formato 2008).
Uso: python -I preparar_entrada.py <saida> <extraidos> [<pasta extra> ...]
"""
import hashlib, os, shutil, struct, sys
from pathlib import Path

saida = Path(sys.argv[1])
fontes = [("containers", Path(sys.argv[2]))] + [("extra%d" % k, Path(p)) for k, p in enumerate(sys.argv[3:], 1)]
if saida.exists() and any(saida.iterdir()):
    sys.exit("a saida precisa estar vazia")
saida.mkdir(parents=True, exist_ok=True)
vistos = {}
linhas = ["arquivo\tfonte\tnome_original\ttipo\ttamanho\tsha256\tvalido\tmotivo"]
cont = {}
dup = {}
for nome, pasta in fontes:
    if not pasta.is_dir():
        continue
    for f in sorted(pasta.glob("*.bin")):
        d = f.read_bytes()
        h = hashlib.sha256(d).hexdigest()
        cont[nome] = cont.get(nome, 0) + 1
        if h in vistos:
            dup[nome] = dup.get(nome, 0) + 1
            continue
        tipo = "?"; ok = True; motivo = ""
        if len(d) < 24:
            ok = False; motivo = "curto"
        else:
            firma, virt, fis = struct.unpack(">III", d[:12])
            if (firma & ~1) != 0x102A1100: ok = False; motivo = "firma %08X" % firma
            elif virt < 24 or fis == 0 or fis % 4 or virt + fis != len(d): ok = False; motivo = "tamanhos virt=%d fis=%d total=%d" % (virt, fis, len(d))
            tipo = "vs" if firma & 1 else "ps"
        arq = "%s_%s%s.bin" % (tipo if tipo != "?" else "xx", ("" if nome == "containers" else nome[:3] + "_"), f.stem)
        vistos[h] = arq
        shutil.copyfile(f, saida / arq)
        linhas.append("\t".join([arq, nome, f.name, tipo, str(len(d)), h, str(int(ok)), motivo]))
(saida.parent / "entrada.tsv").write_text("\n".join(linhas) + "\n", encoding="utf-8")
print("fontes:", cont, "duplicados de conteudo ignorados:", dup, "unicos:", len(vistos))
