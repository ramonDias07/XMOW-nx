Primeira release do **XMOW-nx**, com o código da build **0.1.16**. **Ainda não é jogável**: esta release é só código-fonte, para quem quer compilar e testar no console. Ela não traz NRO nem arquivos do jogo; o código e os shaders são gerados a partir do seu próprio disco.

## Onde o port está

- **Testado no Switch físico até a 0.1.13:** logos e vídeos Bink com áudio, menus com imagem 3D, cutscenes e o gameplay da primeira fase (selva) com HUD e o quadro inteiro.
- **Imagem:** a tela dividida na linha 512 foi resolvida na 0.1.9 e as manchas brancas na 0.1.10. Ainda faltam a névoa de altura, que está omitida por enquanto, e o clarão na abertura da fase.
- **Desempenho:** 15 a 28 FPS no gameplay, com a GPU entre 35 e 70 ms por quadro (0.1.12).
- **Ainda não testadas no console:** a 0.1.14 a 0.1.16 (menu novo e os primeiros ajustes de desempenho).

## O que mudou desde a 0.1.9

- **0.1.10:** o *predicated tiling* é desligado no próprio jogo (`GUseTilingMode = 0`, novo passo `tools/codegen/patch_xmow_jogo.py`, como no singularity-nx), e entraram 7 patches do SDK do singularity-nx (resultados de oclusão, espera de threads, clears de estêncil, 32.768 descritores de textura, fusão de profundidade, descritores no envio parcial e vértices no cache).
- **0.1.11:** sem pré-passo de Z e sem pré-passo mascarado (o chão voltou).
- **0.1.12 e 0.1.13:** diagnóstico da névoa e dos translúcidos; a névoa de altura (PS 2639) fica omitida por enquanto (`xmow_omitir_ps`).
- **0.1.14 e 0.1.15:** menu rápido (`+` e `−`) e ajustes avançados (`L + R + →`) no padrão do Budokai3-nx, vsync relaxado, GPU boost oficial, resolução de saída e o clear de profundidade em float24.
- **0.1.16:** diagnósticos desligados por padrão; ganchos de CPU na espera da GPU, nas consultas de oclusão e no cache de shaders; destinos, resolves e barreiras mais leves no Deko3D.

## Como compilar

Você precisa de um dump `.iso` próprio de **X-Men Origins: Wolverine – Uncaged Edition (USA, Europe)** de Xbox 360, sem title update. Siga o [guia de compilação](https://github.com/ramonDias07/XMOW-nx/blob/v0.1.16/docs/build.md): scripts PowerShell no Windows com Python, CMake, Ninja, LLVM-MinGW, DXC e devkitPro. Um instalador de um clique, como o do Budokai3-nx, virá quando o port for jogável.

Se testar, conte a build, a cena e o que aparece na tela, e anexe a pasta `logs/` numa [issue](https://github.com/ramonDias07/XMOW-nx/issues).

## Créditos

- **VasilkovND** pelo gears2-nx, a base do port.
- **panalexeu** pelo singularity-nx: o patch do *predicated tiling* e 7 patches do SDK.
- Os demais projetos estão em [Referências](https://github.com/ramonDias07/XMOW-nx/blob/v0.1.16/docs/referencias.md) e [CONTRIBUTORS.md](https://github.com/ramonDias07/XMOW-nx/blob/v0.1.16/CONTRIBUTORS.md).

## English

First XMOW-nx release, with the code of build **0.1.16**. **Not playable yet**: source code only, for people who want to build and test it. No NRO or game data is included. Tested on a physical Switch up to 0.1.13: logos, Bink videos with audio, 3D menus, cutscenes and the first level with HUD and the full frame, at 15–28 FPS. The height fog is still skipped and the level intro has a bright glare. Builds 0.1.14–0.1.16 (new menu, first performance work) are not tested yet. You need your own Xbox 360 *Uncaged Edition (USA, Europe)* ISO without title update.
