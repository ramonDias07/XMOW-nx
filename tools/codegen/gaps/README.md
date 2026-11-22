# Busca de lacunas do codegen

Ferramentas usadas para montar `app/codegen/xmow_config.toml` (rodadas 1 a 6). Não fazem parte do build: servem para
achar funções que a descoberta do ReXGlue não atribuiu e que o jogo chama (ponteiros em tabelas, chamadas de cauda,
endereços montados com `lis`/`addi`).

- `gap_classify.py`, `pointer_scan.py` e `accept_gaps.py` são cópias ajustadas das ferramentas do
  [masseffect-nx](https://github.com/nebadasSwifty/masseffect-nx) (GPL-3.0). `scan_round.sh` usa o `tools/find_gaps.py`
  desse repositório (variável `MASSEFFECT_NX`).
- `xex_image.py` monta a imagem do guest (base `0x82000000`) a partir do seu `default.xex`; precisa do `openssl` no PATH.
- `lisaddi_scan.py` procura pares `lis`+`addi`/`ori` que apontam para o início de uma lacuna (rodada 6).

Rodar dentro de `out/codegen`, depois de um codegen. Cada função aceita entra em `xmow_config.toml` com a evidência
num comentário.
