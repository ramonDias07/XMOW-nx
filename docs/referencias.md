# Referências e proveniência

## Projetos usados

- **gears2-nx** de VasilkovND (GPL-3.0). É a base do port: a camada do app em `app/src/` (`gow2_*`), o leitor do anel de
  comandos, os clears, os aliases de EDRAM, a biblioteca de shaders por microcódigo, o menu, o pipeline de shaders em
  `tools/shaders/` e os passes `patch_leaf_prologue`, `patch_stack_access`, `patch_inline_calls`, `patch_local_regs`,
  `make_nx_pch` e `prepare_tree`. Os patches 0001 a 0050 do SDK dele estão incluídos em `patches/rexglue-nx.patch`. Os
  nomes `gow2_*` foram mantidos para facilitar a comparação. Os hooks com endereços do Gears ficaram de fora
  (`gow2_perf_hooks.cpp`, `gow2_memory_hooks.cpp`, `gow2_game_patches.cpp`), e os ajustes por hash de shader do Gears
  ficam atrás de `gow2_ajustes_gears2 = false`. [gears2-nx](https://github.com/VasilkovND/gears2-nx) · [Avisos de terceiros](../licenses/gears2-nx-THIRD_PARTY_NOTICES.md)
- **gears-of-war-2-pc-port** de Santiago Quiroz Upegui e colaboradores (BSD-3-Clause). Os passes `patch_switch_traps`,
  `patch_memory_barriers` e `patch_spin_hints` em `tools/codegen/` são cópias com os nomes de arquivo trocados para
  `xmow_*`. O `patch_switch_traps` vem, por sua vez, do gears-judgement-recomp da OverkillLabs3. [Port PC](https://github.com/santiquiroz/gears-of-war-2-pc-port) ·
  [gears-judgement-recomp](https://github.com/OverkillLabs3/gears-judgement-recomp) · [Licença](../licenses/gow2-pc-port-BSD.txt)
- **ReXGlue SDK NX**. O patch do SDK aplica-se sobre o commit `f961303fe3c6e3a90905b379ff4bf3de9a35a757`. O ReXGlue
  deriva do Xenia. [Commit do SDK NX](https://github.com/nx-mod/rexglue-sdk-nx/commit/f961303fe3c6e3a90905b379ff4bf3de9a35a757) ·
  [ReXGlue](https://github.com/rexglue/rexglue-sdk)
- **XenosRecomp** da hedge-dev (MIT), na versão modificada do gears2-nx. Este port acrescenta 256 constantes de pixel
  shader, três *cubemaps* por shader e uma correção para *vertex fetch* com máscara de destino vazia.
  [XenosRecomp](https://github.com/hedge-dev/XenosRecomp)
- **masseffect-nx** de nebadasSwifty (GPL-3.0). A busca de lacunas do codegen em `tools/codegen/gaps/` usa cópias
  ajustadas das ferramentas dele, e a memória reservada em blocos de 2 MB seguiu o mesmo caminho.
  [masseffect-nx](https://github.com/nebadasSwifty/masseffect-nx)
- **LoS1-nx** e **Budokai3-nx**. `tools/extract_iso.py` (extração XDVDFS) vem da árvore do LoS1-nx pelo Budokai3-nx. As
  primeiras builds (0.0.x) usavam o renderizador do Budokai3-nx; o atalho `L3 + R3` de captura e o `toolchain.cmake`
  vêm de lá. [LoS1-nx](https://github.com/FurryLilManChild/LoS1-nx) · [Budokai3-nx](https://github.com/ramonDias07/Budokai3-nx)
- **SPIRV-Cross**, **uam** (devkitPro e SwitchWakerHD), **xxHash**, **fmt** e **libmspack**, buscados pelo
  `tools/switch_setup.py` do SDK em commits fixos. [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross) ·
  [uam](https://github.com/devkitPro/uam) · [SwitchWakerHD](https://github.com/centollOS/SwitchWakerHD)

## O que é deste port

- O manifesto e as funções extras do Wolverine em `app/codegen/` (setjmp/longjmp, seis rodadas de lacunas, com a
  evidência de cada entrada) e os nomes dos `Create*Shader` do D3D do jogo em `names.toml`.
- Os patches do SDK para o Wolverine dentro de `patches/rexglue-nx.patch`: dispositivo `cache:` do UE3, 32 descritores
  de *vertex fetch*, redirecionamento de resolve de tesela (*predicated tiling*), captura `L3 + R3`, 256 constantes de
  pixel shader e captura da EDRAM na cópia das teselas.
- Na camada do app: emulação de clears do D3D pelo retângulo do jogo, cor do clear vinda do vértice, retângulos que leem
  textura expandidos em quads, MSAA 2X tratado como 1X e o extrator de shaders para os pacotes `.xxx` do Wolverine
  (`tools/shaders/src/xmow_extract_shaders.cpp`).
- O pipeline de shaders portado para Windows: trabalhadores em processos separados com vigia de tempo para o
  `ds_dksh` e o validador `ds_validar`.

## Como projetos semelhantes pedem o jogo

| Projeto | Entrada e instalação |
| --- | --- |
| [gears2-nx](https://github.com/VasilkovND/gears2-nx) | Disco retail sem title update. Código, shaders e NRO são gerados no computador de quem compila. |
| [Budokai3-nx](https://github.com/ramonDias07/Budokai3-nx) | ISO USA/Japan; o build gera C++, shaders e NRO localmente. |
| XMOW-nx | ISO da Uncaged Edition (USA, Europe), sem title update. O build gera C++, shaders e NRO localmente; o disco extraído vai em `game_root/` ao lado do NRO. |
