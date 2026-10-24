# Estante — manual do aplicativo nativo de PS5

Title ID: PPSA99841. A organização existe dentro do aplicativo e não modifica o app.db,
o menu oficial ou os arquivos dos jogos. Autor: Kauan (@kndofc).

## Navegação

- L1/R1: Início, Biblioteca, Coleções, Ajustes.
- Início (catálogo): o jogo em foco ocupa a tela, com a arte larga ao fundo quando existe em
  `/data/estante/covers/wide`. Esquerda/direita move na fileira; cima/baixo troca de fileira. X abre
  o jogo; para títulos importados de emulador, abre o emulador e você escolhe o título lá. Círculo
  cancela a animação, R2 abre a página do jogo, Triângulo organiza, Quadrado busca,
  Options abre a Biblioteca.
- Fileiras do Início: Continuar jogando (somente jogos iniciados pelo Estante; PS2 registra a imagem
  escolhida), Favoritos, Quero jogar, coleções fixadas e sistemas com jogos: PS5, PS4, PS1, PS2,
  GameCube/Wii, Wii U, 3DS e DS. Fileiras vazias e apps não aparecem. L2 + R2 alterna Catálogo/Estante.
- Biblioteca: grade de ícones quadrados, seis por linha. Cima na primeira linha foca os chips (busca, visão,
  ordem, filtros); X em um chip o aciona. Esquerda na borda abre a lista de visões.
- Detalhes do jogo: página inteira, sem janela. Jogar, estrela de favorito e menu (esquerda/direita e X).
  Triângulo abre o menu, Quadrado favorita, Círculo volta. Só aparecem dados locais que existem: plataforma,
  ID, tempo, última execução, status, coleções e troféus por grau (platina, ouro, prata e bronze, lidos do
  TRPTITLE.DAT do console; jogos de PS5/PS4, nunca PS2).
- Círculo: voltar. Na Biblioteca, volta à barra lateral; novamente, ao Início.
- Triângulo: organizar no Início/detalhes; organizar seleção quando há itens marcados.
- Quadrado: buscar no Início; marcar/desmarcar jogos na Biblioteca.
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
- `catalog.json`: catálogo para consulta offline.
- `estante-preferences.json`: idioma, tema, estilo do Início, dica do atalho, texto maior, animação e modo gravação.
- `estante-cover-00.bin` a `estante-cover-31.bin`: cache de 32 imagens; cada arquivo tem limite de 4 MiB,
  então o total teórico pode chegar a 128 MiB. O uso real depende do tamanho das capas baixadas.
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

Até 1024 títulos e 32 coleções. O app lê apenas as pastas padrão em `/data` dos emuladores:
PS5SX2 (`/data/PCSX2/games`), SwanStationPS5 (`/data/SwanStationPS5/games`), Porpoise
(`/data/porpoise/games`) e PS5-ECAM (`/data/ps5cemu/library.json`). Abra os emuladores uma vez
antes de atualizar a biblioteca. USB/M.2 e Switch não são importados; 3DS/DS dependem de o ECAM
ter escaneado os jogos. PS2 portado como PS4 precisa de classificação manual. ISOs do PS5SX2
não aparecem como apps do catálogo. O app não mede tempo de jogo, não lê histórico global e não
sincroniza dados na nuvem.

A interface é renderizada na GPU com OpenGL. O preview no PC valida layout e fluxo de renderização,
mas não substitui teste de desempenho, estabilidade ou suspensão/reabertura no console. Testes host
cobrem metadados, migração, filtros, ordenação, navegação e decodificação de imagens, com
ASan/UBSan nos testes de dados e imagens.

## Créditos

Base: BlackBearReloaded/ps5-native-app-boilerplate, GPL-3.0-or-later.
cJSON 1.7.19, MIT. SDK público ps5-payload-dev. Inter, SIL Open Font License 1.1.
