# Estante 0.3 — validação em 17/10/2026

Código nativo: commit `578ff26`, branch `feat/home-experience`.

## Confirmado

- Suítes de catálogo/metadados, filtros/coleções e imagens passaram com ASan/UBSan.
- Preview do renderizador nativo em dez estados passou com ASan/UBSan: início,
  biblioteca, coleções, ajustes, detalhes, menu, busca, texto maior, gravação e vazio.
- Conferência visual de início, biblioteca, coleções, detalhes, menu e texto maior.
- Build PS5 concluído; integridade do eboot e runtime verificada pelo tooling.
- MkPFS 1.1.0 verificou a imagem de 786432 bytes: zero erros e zero avisos.
- Envio ao console com cópia anterior preservada e SHA-256 de retorno idêntico.
- Log do PS5 FW 11.60 confirmou `BOOT ESTANTE HOME PPSA99841`, `CONTROLE ABERTO`
  e `Seus jogos estão prontos para organizar.`
- API de estado confirmou PPSA99841 em execução; fonte atlas presente no app montado.

Imagem enviada SHA-256:
`740ed31909624253dd325c675199934c2430b817ba5dcf13d044a86b8aacfa8d`.
Executável SHA-256:
`45891f0696b9070c0e66bc1c9018686eba17ad98fa1b29f574e934b0b2984b98`.

## Ainda não confirmado no aparelho

- Fluidez da navegação, modo de texto maior e leitura a distância na TV.
- Criar/renomear coleções, salvar seleção, migrar dados v1 e reabrir preservando dados.
- Progresso manual, coleções automáticas e preferências após reinício.
- Abertura de um jogo pelo Estante, bloqueio quando outro jogo está ativo e retorno do sistema.
- Comportamento em outros firmwares ou outras combinações de payloads.

Uma tentativa inicial pela API retornou HTTP 503. A abertura posterior foi confirmada
pelos logs; a causa da falha inicial não foi determinada. Não atribuir ao app ou ao
payload sem mais evidências. A instalação não alterou payloads, app.db, jogos ou saves.

Não é uma release estável; main, tags e assets da release anterior permanecem intactos.
