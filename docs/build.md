# Compilar e instalar

> [!WARNING]
> O port está em desenvolvimento e **ainda não é jogável**. Estas instruções servem para quem quer acompanhar,
> testar builds de diagnóstico ou contribuir. Veja o [estado atual](estado.md).

Código-fonte da build **0.1.34f**. O build parte do disco **X-Men Origins: Wolverine – Uncaged Edition (USA, Europe)** de Xbox 360, sem title update. O
arquivo de entrada é `default.xex` com SHA-256 `2f987f7ba3415bf9d1bc63bac3e671fd534eb33bb53878e816500c01bcd15378`
(Title ID `41560816`). O código recompilado, os shaders e o NRO são gerados no seu computador a partir do seu dump.

Os scripts atuais são para **Windows (PowerShell 5.1 ou superior)**, que é onde o port é desenvolvido. Os passos de
Python e CMake funcionam em outros sistemas, mas ainda não há um script único para Linux ou macOS.

## 1. Ferramentas

- Git, CMake 3.25 ou superior, Ninja e Python 3.11 ou superior.
- [LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw/releases) (`clang`, `clang++`) para as ferramentas de PC.
- [DirectXShaderCompiler](https://github.com/microsoft/DirectXShaderCompiler/releases) (`dxc.exe`) e `spirv-val`
  (SPIRV-Tools; o pacote `mingw-w64-clang-x86_64-spirv-tools` do MSYS2 serve).
- [devkitPro](https://devkitpro.org/wiki/Getting_Started) com devkitA64, libnx e as ferramentas Switch.
- O gerador de código (`rexglue.exe`) da 0.1.34f é o do SDK do fork LoS1-nx (commit `3426a45`) mais `patches/los1-nx-generator.patch`; o
  `tools/prepare_generator.py` (etapa 4) baixa, aplica o patch e compila com o LLVM-MinGW. As builds até a 0.1.16 usaram o
  `rexglue` de host do ReXGlue SDK 0.10 (`-GeradorAntigo`). No Windows, a pasta do gerador e a `bin` do LLVM-MinGW precisam
  estar no `PATH` (DLLs do runtime).

## 2. Preparar o SDK

Na raiz do repositório:

```sh
python tools/prepare_sdk.py
```

O script baixa o `rexglue-sdk-nx` no commit `f961303`, aplica `patches/rexglue-nx.patch` (camada do gears2-nx, patches 0001 a 0050, mais os patches do XMOW 1001 a 1041
em um só arquivo), roda o `tools/switch_setup.py` do próprio SDK, que busca SPIRV-Cross e uam em commits fixos e monta o
FFmpeg do Switch, e por fim aplica `patches/uam-xmow.patch` (uma correção no uam, que só existe depois dessa etapa). O resultado fica em `sdk/`.

## 3. Extrair o disco

```sh
python tools/extract_iso.py "/caminho/X-Men Origins - Wolverine - Uncaged Edition (USA, Europe).iso"
```

Os arquivos vão para `assets/game_root/` (`default.xex` e `WGame/`, cerca de 6,9 GB).

## 4. Gerar o código recompilado

Primeiro o gerador (uma vez; cerca de 10 minutos, com a busca das dependências):

```powershell
python tools/prepare_generator.py --llvm-mingw C:\caminho\llvm-mingw\bin --ninja C:\caminho\ninja.exe
```

O resultado é `out/generator/los1-nx/sdk/out/win-amd64/rexglue.exe`. Depois o codegen:

```powershell
powershell -ExecutionPolicy Bypass -File tools/codegen.ps1 `
  -Rexglue out\generator\los1-nx\sdk\out\win-amd64\rexglue.exe -Python python -ExtraPath C:\caminho\llvm-mingw\bin
```

O manifesto, as funções extras e as opções do gerador ficam em `app/codegen/` (`xmow_flavor_lote1.toml` e a lista de
exceções `local_regs_keep.txt`). O script roda o codegen (cerca de 61 mil funções) e os passes sobre o código gerado: traps
de switch, barreiras de memória, *spin hints*, prólogo de folhas, acesso à pilha, chamadas inline, registradores locais
(`patch_local_regs`, com a lista de exceções), leituras e escritas sem `volatile` e barreiras de laço. A saída fica em
`out/codegen/generated/default/` (cerca de 290 MB) e não vai para o Git. O PCH do Switch é feito no configure do CMake
(`app/tools/make_nx_pch.py`, com `app/tools/xmow_diet_ops.h`).

Receita da 0.1.34f: sem nenhum dos parâmetros `-Sem...`. `-SemLocalRegs`, `-SemVolatil` e `-GeradorAntigo` voltam aos lotes
anteriores (as builds até a 0.1.16 foram feitas com `-SemLocalRegs -SemVolatil -GeradorAntigo`; veja o teste 6 em [estado](estado.md)).

## 5. Gerar os shaders

```powershell
powershell -ExecutionPolicy Bypass -File tools/shaders.ps1 -LlvmBin C:\caminho\llvm-mingw\bin `
  -Dxc C:\caminho\dxc.exe -SpirvVal C:\caminho\spirv-val.exe -Python python
```

O pipeline é o do gears2-nx portado para Windows: extrai os containers de shader dos pacotes `.xxx` do disco, traduz
com o XenosRecomp do gears2 (HLSL), compila para SPIR-V com o DXC, empacota em `xmow_shaders.dslib` e compila o cache
DKSH (`shadercache_dksh.bin`) com o uam. Depois valida os dois arquivos como o runtime lê. A etapa DKSH leva cerca de
25 minutos.

Alguns shaders só existem dentro do XEX ou são alterados pelo jogo no momento do uso. O port grava esses containers em
`shaders_dump/` ao lado do NRO quando não os acha na biblioteca. Para incluí-los, copie essa pasta do SD e passe-a com
`-Extras C:\caminho\shaders_dump`, depois rode o script de novo.

## 6. Compilar o NRO

```powershell
powershell -ExecutionPolicy Bypass -File tools/build.ps1 -DevkitPro C:\devkitPro -Python python -Jobs 8
```

O CMake espelha o código gerado com o PCH do Switch, compila para ARM64 e monta o pacote. A saída usa o ícone padrão do
libnx; para usar um JPEG próprio de 256 × 256, acrescente `-Icone C:\caminho\icone.jpg`. A compilação completa leva cerca
de 40 minutos. O ELF fica em `out/elf/`, com o nome do hash do NRO, para simbolizar logs de falha com
`aarch64-none-elf-addr2line`.

## 7. Copiar para o Switch

Copie `out/sd/switch/XMOW/` para `/switch/XMOW/` no cartão e acrescente a pasta `assets/game_root/` como
`/switch/XMOW/game_root/`.

```text
/switch/XMOW/
  XMOW.nro
  xmow.toml
  xmow_shaders.dslib
  shadercache_dksh.bin
  game_root/default.xex
  game_root/WGame/...
```

O port precisa da memória do modo aplicativo: abra pelo Homebrew Menu segurando `R` ao iniciar um jogo, ou por um
forwarder NSP. Logs, capturas e despejos de diagnóstico ficam em `/switch/XMOW/logs/`, `volcado_NN/` e
`shaders_dump/`. `assets/`, `sdk/` e `out/` são resultados locais e ficam fora do Git.
