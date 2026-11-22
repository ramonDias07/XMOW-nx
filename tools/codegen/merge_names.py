#!/usr/bin/env python3
"""Soma app/codegen/names.toml ao manifesto (como o codegen.sh do gears2-nx).
Uso: merge_names.py <manifesto_base> <config_base> <names.toml> <manifesto_saida> <config_saida> <sdk> <default.xex>
- copia os dois TOML, troca file_path/out_directory_path do manifesto;
- cada [entrypoint.functions.0xA] de names.toml: remove a linha '0xA = ' solta do manifesto/config e anexa a tabela nomeada ao manifesto;
- recusa nome que o SDK ja define como __imp__<nome>.
"""
import re, sys, subprocess
from pathlib import Path
base, cfg, names, out_m, out_c, sdk, xex = sys.argv[1:8]
m = Path(base).read_text(encoding="utf-8"); c = Path(cfg).read_text(encoding="utf-8")
extra = Path(names).read_text(encoding="utf-8")
m = re.sub(r"^file_path = .*$", "file_path = " + repr(Path(xex).resolve().as_posix()).replace("'", "\""), m, flags=re.M)
m = re.sub(r'^out_directory_path = .*$', 'out_directory_path = "generated/default"', m, flags=re.M)
for n in re.findall(r'^name = "(.*)"$', extra, re.M):
    hit = subprocess.run(["git", "-C", sdk, "grep", "-qE", rf"__imp__{n}\b", "--", "src", "include"]).returncode == 0
    if hit:
        sys.exit(f"names.toml: o SDK ja define __imp__{n}; escolha outro nome")
addrs = re.findall(r'^\[entrypoint\.functions\.(0x[0-9A-Fa-f]+)\]', extra, re.M)
for a in addrs:
    pat = rf'^{a}\s*=[^\n]*\n'
    m = re.sub(pat, '', m, flags=re.M | re.I); c = re.sub(pat, '', c, flags=re.M | re.I)
if addrs:
    m = m.rstrip() + "\n\n" + extra
Path(out_m).write_text(m, encoding="utf-8", newline="\n"); Path(out_c).write_text(c, encoding="utf-8", newline="\n")
print(f"names.toml: {len(addrs)} nomes somados")
