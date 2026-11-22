# Estado e histórico dos testes

Todas as builds até agora são de diagnóstico (`-diag`). Nenhuma é jogável. Os testes foram feitos num Switch físico,
em modo portátil, abrindo o port por forwarder NSP.

## Onde o port está (0.1.9-diag)

- **Funciona no console:** logos e vídeos Bink com áudio, menus do jogo com imagem 3D, cutscenes e a entrada no
  gameplay (fase da selva, `jungle1`), com o HUD.
- **Imagem errada no gameplay.** O teste 14 achou a causa da tela dividida na linha 512. A camada do gears2-nx trata toda
  superfície MSAA como um alias 1X de altura dupla, enquanto o Wolverine desenha a cena em MSAA 2X com duas teselas. A
  0.1.9 desliga esse alias para MSAA 2X (`xmow_msaa2x_como_1x`), mas **ainda não foi testada**. A metade de cima da cena
  também sai com brilho estourado, provavelmente pela mesma causa.
- **Desempenho:** 5 a 18 FPS com 1.700 a 8.000 desenhos por quadro. A GPU leva 50 a 140 ms por quadro. A otimização
  só começa depois que a imagem estiver certa.
- **Desligado por enquanto:** consultas de oclusão reais (escondiam o herói enquanto a profundidade estava errada).

## Histórico

| Build | Resultado no Switch | O que mudou depois |
| --- | --- | --- |
| 0.0.1 | Sem imagem; áudio toca e trava | Containers UE3 sem tabela de constantes passam a ser aceitos; rodada 6 do codegen (thunk montado com `lis`/`addi`). |
| 0.0.2 | Vídeos Bink verdes e vermelhos; trava ao carregar a fase | Memória reservada em blocos de 2 MB (limite de mapeamentos do Horizon); texturas derivadas das instruções `tfetch`; planos YUV como texturas dinâmicas. |
| 0.0.3 | Logos, vídeos e loading aparecem; menu e jogo pretos | Biblioteca completa de shaders, lida do SD sob demanda (16.437 shaders). |
| 0.0.4 | Crash no SPIRV-Cross | SPIR-V guardado fora de um `std::span` temporário. |
| 0.0.5 | Menu principal aparece; falha da GPU ao carregar a fase | Troca da base para a camada do gears2-nx (leitor do anel próprio, aliases de EDRAM, clears). |
| 0.1.0 | Tela parada, sem desenhos | A thread principal caía num *funclet* de exceção: builds seguintes sem o passe `patch_local_regs`. |
| 0.1.1 | **Chega ao gameplay**, só as 512 linhas de cima da cena | Resolve de tesela redirecionado para a textura certa (*predicated tiling* em 2 telas). |
| 0.1.2 | Cena estoura em branco; trava ao carregar a selva | 11 mil desenhos de retângulo rejeitados eram clears do D3D: passam a ser emulados. |
| 0.1.3 | Silhuetas tingidas de vermelho e roxo | Clear emulado limitado ao retângulo e à tesoura do jogo; oclusão real desligada. |
| 0.1.4 | Cena em cor chapada, só partículas e HUD | Atalho `L3 + R3` de captura. |
| 0.1.6 | Captura mostra a cena 3D certa antes do vermelho | Retângulos que leem textura deixam de ser clears; 256 constantes de pixel shader. |
| 0.1.7 | Vídeos e cutscenes certos; menu 3D; tela dividida na linha 512 | Cor do clear vinda do vértice; captura da EDRAM na cópia da tesela de baixo. |
| 0.1.8 | Causa da tela dividida achada (alias MSAA 2X) | 0.1.9: MSAA 2X vira 1X sem alias. |
| 0.1.9 | Não testada | — |

## Como ajudar nos testes

Copie a pasta do SD como em [Compilar e instalar](build.md), abra o port e traga de volta `logs/`, `volcado_NN/` e
`shaders_dump/`. `L3 + R3` grava uma captura do próximo quadro. Conte a build, a cena e o que aparece na tela.
