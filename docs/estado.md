# Estado e histórico dos testes

Até a 0.1.15 as builds eram de diagnóstico (`-diag`); a 0.1.16 é a primeira com os diagnósticos desligados. Nenhuma é jogável ainda. Os testes foram feitos num Switch físico,
em modo portátil, abrindo o port por forwarder NSP.

## Onde o port está (0.1.16)

- **Funciona no console:** logos e vídeos Bink com áudio, menus do jogo com imagem 3D, cutscenes e o gameplay na fase
  da selva (`jungle1`), com o HUD e o quadro inteiro (a tela dividida na linha 512 foi resolvida na 0.1.9).
- **Imagem do gameplay quase certa.** As manchas brancas sumiram com o *predicated tiling* desligado (0.1.10) e o chão
  voltou sem os pré-passos de Z (0.1.11). A névoa de altura (PS 2639) ainda sai estourada e é omitida por enquanto
  (`xmow_omitir_ps`), e a abertura da fase ainda tem um clarão.
- **Desempenho:** 15 a 28 FPS no gameplay, com a GPU entre 35 e 70 ms por quadro (teste 18). A 0.1.16 traz os primeiros
  ajustes de desempenho, ainda sem teste no console.
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
| 0.1.9 | **Tela dividida resolvida**: quadro inteiro, Wolverine visível; manchas brancas e névoa estourada na selva | 0.1.10: *predicated tiling* desligado no próprio jogo (`GUseTilingMode = 0`, como o singularity-nx) e 7 patches do SDK do singularity-nx; captura A/B por grupo de desenhos. |
| 0.1.10 | Manchas brancas sumiram; chão e fundo do título só na profundidade (pretos) | 0.1.11: sem pré-passo de Z e sem pré-passo mascarado. |
| 0.1.11 | Chão de volta; a névoa (PS 2639) e os translúcidos estouram a abertura | 0.1.12: mais log de constantes e dos efeitos de tela inteira. |
| 0.1.12 | Gameplay certo sem a névoa 2639; 15 a 28 FPS (GPU 35 a 70 ms) | 0.1.13: névoa 2639 omitida (contorno, não correção); menos capturas automáticas. |
| 0.1.13 | Gameplay com cor; a abertura ainda estoura | 0.1.14: menu rápido e avançado no padrão dos outros ports, vsync relaxado, GPU boost oficial, resolução de saída. |
| 0.1.14 / 0.1.15 | Não testadas | 0.1.15: menu igual ao do Budokai3-nx; diagnóstico do perfil da GPU; valor do clear de Z em float24. |
| 0.1.16 | Não testada | Diagnósticos desligados; ganchos de CPU (espera da GPU, consultas de oclusão, cache de shaders); destinos, resolves e barreiras mais leves no Deko3D (patch 1017). |

## Como ajudar nos testes

Copie a pasta do SD como em [Compilar e instalar](build.md), abra o port e traga de volta `logs/`, `volcado_NN/` e
`shaders_dump/`. `L3 + R3` grava uma captura do próximo quadro. Conte a build, a cena e o que aparece na tela.
