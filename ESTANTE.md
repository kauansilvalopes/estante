# Estante 0.3 — aplicativo nativo de PS5

Title ID: PPSA99841. A organização existe dentro do aplicativo e não modifica o app.db,
o menu oficial ou os arquivos dos jogos. Autor: Kauan (@kndofc).

## Navegação

- L1/R1: Início, Biblioteca, Coleções, Ajustes.
- Direcional: mudar foco. No Início, cima/baixo alterna jogos e coleções.
- X: abrir detalhes, coleção ou confirmar uma ação. Nos detalhes, X solicita iniciar o jogo.
- Círculo: voltar. Na Biblioteca, volta à barra lateral; novamente, ao Início.
- Triângulo: organizar no Início/detalhes; buscar na Biblioteca; organizar seleção quando há itens marcados.
- Quadrado: marcar/desmarcar jogos na Biblioteca. Triângulo abre ações para a seleção.
- Options: biblioteca completa no Início; menu contextual nas demais telas.
- R3: modo gravação. Também disponível em Ajustes, junto com texto maior.
- Teclado: X digita, Quadrado apaga um caractere, Options salva, Círculo cancela.

Para preencher uma coleção: entre em Coleções, foque a coleção, Options > Escolher jogos.
Marque os títulos e use Options > Salvar seleção. Círculo cancela a edição.
Coleções automáticas usam plataforma, progresso e favoritos definidos em Filtros, a partir
 de Todos os jogos e sem busca textual ativa. Elas não possuem seleção manual.

## Persistência e migração

Dados ficam em `/download0` do próprio Estante:

- `estante.json`: organização v2, com favoritos, ocultos, pastas, fixados e progresso manual.
- `estante-before-v2.json`: cópia v1 antes da primeira migração. Não é sobrescrita.
- `estante-backup.json`: estado anterior à última alteração.
- `estante-export.json`: cópia independente pedida em Ajustes.
- `estante-preferences.json`: texto maior e modo gravação.
- `catalog.json`: catálogo para consulta offline.
- `estante-cover-00.bin` a `estante-cover-31.bin`: cache limitado de imagens, até cerca de 32 MiB.
- `artwork/<TITLEID>.png`: arte opcional fornecida pelo usuário; não há importador visual ainda.

A versão antiga não lê o formato v2. Para voltar ao app anterior, preserve os dados atuais
e restaure uma cópia v1; não sobrescreva os metadados sem backup.
Erros de leitura/migração bloqueiam a gravação. Exportar metadados não exporta jogos ou saves.

## Serviço local e instalação

Elf Arsenal em `127.0.0.1:6969`: `/appdb`, `/appdb/icon?id=ID`, `/api/state`, `/launch?titleId=ID`.
Outro jogo aberto bloqueia o pedido de lançamento. HTTP 2xx não comprova abertura:
o app tenta confirmar pelo estado, mas pode ser suspenso pelo sistema ao abrir o título.

Build: `make`. Saídas `dist/PPSA99841/` e `dist/PPSA99841.zip`.
Para imagem, com MkPFS 1.1.0 instalado em ambiente isolado:

```sh
python -m mkpfs pack folder --version PS5 --inode-bits 32 --verify dist/PPSA99841 dist/ffpfsc/PPSA99841.ffpfsc
```

Use a pasta `/data/homebrew/PPSA99841` OU a imagem `/mnt/ext1/PPSA99841.ffpfsc`.
Não mantenha as duas instalações simultaneamente. Feche o aplicativo antes de substituir
arquivos e preserve uma cópia da versão anterior. A montagem pode exigir nova sessão da
ShadowMount. O script antigo `deploy-estante.py` serve apenas para instalação nova em pasta.
O formato ffpfsc continua sujeito às limitações experimentais da ShadowMount.

## Limites e validação

Até 1024 títulos e 32 coleções. Teclado na tela insere letras latinas básicas e números;
a leitura e exibição suportam acentos. PS2 portado como PS4 precisa de classificação manual.
ISOs do PS5SX2 não aparecem como apps do catálogo. Não há leitura de progresso real,
tempo jogado, histórico global, sincronização na nuvem ou download de capas externas.

O renderizador ainda usa CPU e redesenha três quadros quando o estado muda, cobrindo os
buffers. Desempenho e suspensão/reabertura precisam de validação no console; o preview
não substitui esses testes. Testes host cobrem metadados, migração, filtros, ordenação,
navegação e decodificação de imagens, com ASan/UBSan nos testes de dados e imagens.

## Créditos

Base: BlackBearReloaded/ps5-native-app-boilerplate, GPL-3.0-or-later.
cJSON 1.7.19, MIT. SDK público ps5-payload-dev. Noto Sans, SIL Open Font License 1.1.
