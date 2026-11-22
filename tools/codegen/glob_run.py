"""Executa um script Python com glob expandido (Windows nao expande * no shell).
Uso: glob_run.py <script> [args...] -- <glob de arquivos>   ou   glob_run.py <script> [args...] <glob>  (ultimo arg e glob)
Ex.: glob_run.py patch_leaf_prologue.py generated/default/xmow_recomp.*.cpp
Opcoes antes do script: nenhuma. Se houver '--' em args, o glob vai depois dele e o '--' e repassado ao script."""
import glob, subprocess, sys
script, rest = sys.argv[1], sys.argv[2:]
if "--" in rest:
    i = rest.index("--"); pre, pats = rest[:i], rest[i+1:]; sep = ["--"]
else:
    pre, pats, sep = rest[:-1], rest[-1:], []
files = sorted(f for p in pats for f in glob.glob(p))
if not files:
    sys.exit("glob sem arquivos: " + " ".join(pats))
# o glob do patch_switch_traps.py e passado como texto (ele mesmo expande)
sys.exit(subprocess.call([sys.executable, "-I", script, *pre, *sep, *files]))
