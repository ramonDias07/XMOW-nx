# XMOW-nx — X-Men Origins: Wolverine para Nintendo Switch

Port homebrew não oficial de **X-Men Origins: Wolverine – Uncaged Edition** para Nintendo Switch, a partir da edição de
Xbox 360. O ReXGlue recompila o código PowerPC do jogo para ARM64. A camada do app vem do
[gears2-nx](https://github.com/VasilkovND/gears2-nx): ela processa os comandos gráficos Xenos e desenha com Deko3D a
partir dos shaders traduzidos do próprio jogo.

> [!WARNING]
> **Em desenvolvimento: ainda não é jogável.** O jogo chega ao gameplay no console com o quadro inteiro, mas ainda há
> defeitos de imagem (névoa e clarão) e roda a 15–28 FPS. A release v0.1.16 é só código-fonte, para quem quer compilar e
> testar. [Estado e histórico dos testes](docs/estado.md)

[Estado atual](docs/estado.md) · [Como compilar](docs/build.md) · [Capturas](#capturas-no-switch) · [Referências](docs/referencias.md) · [English overview](#english-overview)

## Estado do port

Build atual: **0.1.16** ([release](https://github.com/ramonDias07/XMOW-nx/releases/tag/v0.1.16), só código-fonte).

| Área | Estado |
| --- | --- |
| Switch físico | Testadas no console até a 0.1.13: logos e vídeos Bink com áudio, menus com imagem 3D, cutscenes e o gameplay da primeira fase com HUD e quadro inteiro. **Ainda não testadas:** a 0.1.14 a 0.1.16 (menu novo e ajustes de desempenho). |
| Imagem | A tela dividida (0.1.9) e as manchas brancas (0.1.10) foram resolvidas. Falta a névoa de altura, omitida por enquanto, e o clarão na abertura da fase. |
| Desempenho | 15 a 28 FPS no gameplay, com a GPU entre 35 e 70 ms por quadro (0.1.12). A 0.1.16 traz os primeiros ajustes, ainda sem teste. |
| Shaders | 16.457 shaders do jogo traduzidos (2.127 VS, 14.316 PS e variantes), com cache DKSH lido do SD sob demanda. |

## O que você precisa

- Um dump `.iso` próprio do disco **X-Men Origins: Wolverine – Uncaged Edition (USA, Europe)** de Xbox 360, sem title
  update. O arquivo de entrada é `default.xex` com SHA-256
  `2f987f7ba3415bf9d1bc63bac3e671fd534eb33bb53878e816500c01bcd15378` (Title ID `41560816`).
- Nintendo Switch com acesso ao Homebrew Menu em modo aplicativo (ou um forwarder NSP) e um cartão SD com espaço para
  o disco extraído (cerca de 6,9 GB) e os shaders (cerca de 650 MB).
- Windows com Python, CMake, Ninja, LLVM-MinGW, DXC, devkitPro e o `rexglue` de host, como explica o
  [guia de build](docs/build.md).

O extrator copia o disco para `assets/game_root`. A recompilação e os shaders são gerados localmente a partir do seu
dump. A entrega fica em `out/sd/switch/XMOW/`, e o disco extraído vai em `game_root/` dentro dela.

## O que há no código

| Pasta | Conteúdo |
| --- | --- |
| `app/` | Camada do app (do gears2-nx, adaptada ao Wolverine), CMake do Switch, configuração `xmow.toml` e o manifesto do codegen com as funções extras do jogo. |
| `patches/` | Alterações do ReXGlue SDK NX (camada do gears2-nx e mudanças do XMOW), aplicadas sobre um commit fixo. |
| `tools/` | Preparação do SDK, extração do disco, codegen e passes sobre o código gerado, pipeline de shaders e build. |
| `docs/` | Guia de compilação, estado e histórico dos testes, referências. |

## No Switch

| Comando | Função |
| --- | --- |
| `+` e `−` juntos | Menu rápido do port (resolução de saída, GPU boost oficial, névoa de altura e outras opções). |
| `L + R + →` | Ajustes avançados (renderização, desempenho e diagnóstico). |
| `L3 + R3` | Captura de diagnóstico do próximo quadro (texturas resolvidas em `volcado_NN/` e trace no log). |

O port grava logs em `/switch/XMOW/logs/`. Shaders que faltarem na biblioteca vão para `shaders_dump/`.

## Capturas no Switch

Captura feita no Switch físico, numa build recente: as cutscenes já aparecem com imagem correta. O estado do gameplay está em [Estado atual](docs/estado.md).

![Cutscene de abertura com o título do jogo](docs/screenshots/cutscene.jpg)

## Contribuições

Testes no console, sugestões e pull requests são bem-vindos. Se você testar, conte a build, a cena e o que aparece na
tela, e anexe `logs/` e, se tiver, uma captura `L3 + R3`. Para propor uma correção de renderização, descreva o desenho
ou a passada afetada e, se possível, compare capturas antes e depois na mesma cena.
[Issues](https://github.com/ramonDias07/XMOW-nx/issues) · [Contribuidores](CONTRIBUTORS.md)

Se o projeto for útil, deixe uma estrela no GitHub para acompanhar o repositório.

## Créditos e referências

### Jogo original

- **X-Men Origins: Wolverine** foi desenvolvido pela **Raven Software** e publicado pela **Activision** (2009). A
  *Uncaged Edition* é a versão de Xbox 360 e PlayStation 3. O jogo usa a **Unreal Engine 3** da **Epic Games**.
- **Wolverine** e os **X-Men** são personagens da **Marvel**.

O jogo, os personagens, a arte, a música, as vozes e o design pertencem a essas empresas e às equipes que os criaram. Se
você gosta deste port, apoie as versões oficiais.

### Port e ferramentas

- **VasilkovND** pelo gears2-nx, a base deste port: camada do app, renderizador Deko3D sobre o SDK NX, patches do SDK,
  pipeline de shaders e passes do codegen. [gears2-nx](https://github.com/VasilkovND/gears2-nx)
- **Santiago Quiroz Upegui e colaboradores** pelo port de Gears of War 2 para PC, de onde vêm os passes de traps,
  barreiras de memória e *spin hints*, e **OverkillLabs3** pelo gears-judgement-recomp.
  [gears-of-war-2-pc-port](https://github.com/santiquiroz/gears-of-war-2-pc-port) · [gears-judgement-recomp](https://github.com/OverkillLabs3/gears-judgement-recomp)
- **Tom Clay, colaboradores do Xenia e nx-mod** pelo recompilador, runtime e suporte ao Switch.
  [ReXGlue](https://github.com/rexglue/rexglue-sdk) · [ReXGlue SDK NX](https://github.com/nx-mod/rexglue-sdk-nx)
- **panalexeu** pelo singularity-nx: o patch que desliga o *predicated tiling* no próprio jogo e 7 patches do SDK.
  [singularity-nx](https://github.com/panalexeu/singularity-nx)
- **nebadasSwifty** pelo masseffect-nx, base da busca de lacunas do codegen. [masseffect-nx](https://github.com/nebadasSwifty/masseffect-nx)
- **FurryLilManChild** e colaboradores pelo LoS1-nx, de onde vem o extrator XDVDFS. [LoS1-nx](https://github.com/FurryLilManChild/LoS1-nx)
- **hedge-dev**, **devkitPro**, **SwitchWakerHD** e **Khronos** pelas ferramentas usadas na tradução de shaders.
  [XenosRecomp](https://github.com/hedge-dev/XenosRecomp) · [uam](https://github.com/devkitPro/uam) · [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross)

Veja de onde veio cada parte e o que é deste port em [Referências](docs/referencias.md).

## English overview

**XMOW-nx** is an unofficial Nintendo Switch homebrew port of **X-Men Origins: Wolverine – Uncaged Edition** from
Xbox 360. It uses ReXGlue for static recompilation and the app layer and Deko3D renderer of
[gears2-nx](https://github.com/VasilkovND/gears2-nx). **It is a work in progress and not playable yet.** Up to
build 0.1.13 it was tested on a physical Switch: logos, Bink videos with audio, 3D menus, cutscenes and the first level
with HUD and the full frame, at 15–28 FPS. The height fog is still skipped and the level intro has a bright glare.
Builds 0.1.14–0.1.16 (new menu, first performance work) are not tested yet. Release
[v0.1.16](https://github.com/ramonDias07/XMOW-nx/releases/tag/v0.1.16) is source code only.

Build from source with your own **Uncaged Edition (USA, Europe) Xbox 360 ISO**, no title update. The build tools
(Windows PowerShell for now) generate the code, the shaders and the NRO locally. Hardware test reports and rendering
fixes are welcome in English or Portuguese. [Build guide in Portuguese](docs/build.md) · [Test history](docs/estado.md) ·
[Issues](https://github.com/ramonDias07/XMOW-nx/issues)

**Original game and legal notice.** *X-Men Origins: Wolverine* was developed by **Raven Software** and published by
**Activision**. Wolverine and the X-Men are **Marvel** characters. Unreal Engine is a trademark of **Epic Games**. This is
a non-commercial fan project. It is **not affiliated with or endorsed by** Activision, Raven Software, Marvel, Disney,
Epic Games, Microsoft or Nintendo. This repository contains no game code or assets. You must own the original disc and
make your own dump. Rights holders with a concern can open an issue, and the material will be reviewed or removed
promptly.

## Aviso legal

Este é um projeto de fãs, não oficial e sem fins lucrativos. **Não tem afiliação, patrocínio nem aprovação** da
Activision, Raven Software, Marvel, Disney, Epic Games, Microsoft ou Nintendo.

- *X-Men*, *Wolverine* e os personagens relacionados são marcas e obras protegidas da Marvel. O jogo é © Activision.
  *Unreal Engine* é marca da Epic Games.
- *Xbox 360* é marca da Microsoft Corporation e *Nintendo Switch* é marca da Nintendo. Os nomes aparecem aqui só para
  indicar compatibilidade.
- Este repositório **não distribui** o jogo, seus executáveis, arquivos de dados, áudio, vídeo, texturas, shaders,
  código recompilado nem chaves. Para compilar, é preciso um disco original adquirido legalmente e um dump feito pela
  própria pessoa. Não peça nem compartilhe ISOs ou NROs que contenham dados do jogo.
- Titulares de direitos com alguma objeção podem abrir uma [issue](https://github.com/ramonDias07/XMOW-nx/issues). O
  material será revisto ou removido prontamente.

## Licenças

O código deste port segue a GPL-3.0, a mesma do gears2-nx. Os componentes de terceiros mantêm suas licenças em
[`licenses/`](licenses/) e nas próprias árvores. A GPL-3.0 vale apenas para o código deste repositório e não cobre o
jogo original nem os dados extraídos dele. O dump, os executáveis do jogo, a recompilação gerada, os shaders e caches
derivados, os saves e os NROs gerados ficam no computador de quem compila.
