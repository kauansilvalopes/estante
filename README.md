# Estante

Organizador de biblioteca que roda no proprio PS5, com navegacao pelo controle.

**Prototipo em desenvolvimento.** Compilado e enviado a um PS5 com firmware 11.60;
o registro do titulo foi confirmado, mas abertura, controle, persistencia e lancamento
de jogos ainda aguardam validacao no console. Nao e uma versao estavel.

## Interface com imagens (branch feat/cover-grid)

Esta branch adiciona grade de oito jogos, imagens obtidas da API local do Elf Arsenal,
foco destacado, cache em memoria e carregamento em segundo plano. PNG/JPEG sao
reduzidos para 256 px sem cortar a imagem. Um card com titulo e identificador aparece
quando a imagem esta ausente. Nenhuma capa e publicada no repositorio.

A direcao visual esta registrada em [DESIGN.md](DESIGN.md), com referencia ao getdesign.md.
A tipografia bitmap ainda e uma limitacao; desempenho e aparencia na TV aguardam teste
no PS5. A versao instalada anteriormente no console nao foi atualizada por este commit.

Para inspecionar o desenho real da interface com dados ficticios no computador:
`bash tools/preview-estante.sh` (gera `.local/grid-preview.png`). Isso e uma ferramenta
de desenvolvimento, nao a interface usada pelo jogador.

Teste adicional do decodificador:

```sh
clang-18 -g -fsanitize=address,undefined tests/test_cover_decode.c src/cover_decode.c -lm -o build/tests/test-cover
build/tests/test-cover
```

## Recursos implementados

- Pastas e subpastas dentro do aplicativo (ate 32).
- Favoritos, selecao de varios titulos e classificacao PS2, PS4, PS5 ou outros.
- Teclado na tela para nomes de pastas.
- Organizacao salva localmente em `/download0/estante.json`, com backup.
- Catalogo e pedidos de lancamento pela API local do Elf Arsenal, porta 6969.

As pastas organizam referencias aos jogos; nao alteram o menu oficial nem movem os jogos.
Ports de PS2 instalados como PS4 precisam ser classificados manualmente.
O PC e usado para compilar e enviar; nao hospeda a interface do aplicativo.
O catalogo publicado esta vazio: cada console carrega seus titulos pela API local.

## Compilar (Linux / WSL Ubuntu)

```sh
sudo apt-get update
sudo apt-get install clang-18 llvm-18 lld-18 make ninja-build ccache curl wget unzip
make
```

O build baixa dependencias publicas fixadas por versao e hash. Saidas:
`dist/PPSA99841/` e `dist/PPSA99841.zip`.
O formato atual e aplicativo em pasta para ShadowMount, nao PKG.

## Enviar ao PS5

Com ShadowMount e FTP ativos, envie a pasta para `/data/homebrew/PPSA99841`.
Para uma instalacao nova pelo script, substitua o endereco de exemplo:

```sh
python3 tools/deploy-estante.py --host 192.0.2.10
```

O script usa FTP anonymous na porta 2121, confere os arquivos por SHA-256 e recusa
substituir um titulo ja registrado. Nao troca os payloads instalados.
Aparecendo o titulo Estante, abra pelo menu ou pela Biblioteca de jogos.

## Controles e testes

Direcional navega; X confirma; quadrado seleciona titulos; triangulo/Options abre
as acoes; L1/R1 muda pagina; circulo volta. No teclado, Options salva o nome.
Veja [ESTANTE.md](ESTANTE.md) para detalhes e limitacoes.

Testes da logica de catalogo, classificacao e pastas:

```sh
mkdir -p build/tests
clang-18 -g -fsanitize=address,undefined -c src/cJSON.c -o build/tests/cjson-host.o
clang++-18 -std=c++20 -g -fsanitize=address,undefined tests/test_estante.cpp src/library.cpp build/tests/cjson-host.o -o build/tests/test-estante
build/tests/test-estante
```

Esses testes nao validam o funcionamento no hardware. Os workflows originais da
base estao arquivados em `docs/upstream-workflows`, sem execucao automatica.

## Creditos e licenca

Derivado de [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate),
de BlackBearReloaded, sob GPL-3.0-or-later. Os avisos de autoria foram preservados.
Documentacao original: [docs/UPSTREAM_README.md](docs/UPSTREAM_README.md).

- SDK publico: [ps5-payload-dev/sdk](https://github.com/ps5-payload-dev/sdk), v0.42.
- stb_image: MIT / dominio publico; origem fixada em [third_party/stb/SOURCE.md](third_party/stb/SOURCE.md).
- cJSON 1.7.19: MIT, veja [LICENSE-cJSON](LICENSE-cJSON).
- Licenca do projeto: [LICENSE](LICENSE); demais avisos: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Dependencias baixadas, binarios gerados e dados pessoais do console nao sao versionados.
