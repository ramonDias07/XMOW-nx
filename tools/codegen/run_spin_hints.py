import sys
from pathlib import Path
# Uso: run_spin_hints.py <pasta tools do port PC> <raiz permitida> <arquivos...> (o padrao do script so aceita out/android)
sys.path.insert(0, sys.argv[1])
import patch_spin_hints
sys.exit(patch_spin_hints.main(sys.argv[3:], allowed_root=Path(sys.argv[2]).resolve()))
