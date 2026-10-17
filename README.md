# Estante

Organizador de biblioteca que roda no proprio PS5, com navegacao pelo controle.

**Versão 0.3 em desenvolvimento.** Nova experiência nativa com quatro áreas:
Início, Biblioteca, Coleções e Ajustes. A versão 0.3 iniciou no PS5 do autor (FW 11.60), abriu o controle e carregou o catálogo.
A fluidez, persistência após reabertura e lançamento de jogos ainda precisam de validação de uso.

## Nova experiência

- Início com jogo em destaque, últimas escolhas feitas no Estante, jogos fixados e mosaicos de coleções.
- Biblioteca completa com busca sem distinção de acentos, filtros e ordenação.
- Coleções e subcoleções, seleção múltipla, favoritos, ocultos e classificação PS2/PS4/PS5.
- Coleções automáticas por plataforma, progresso manual e favoritos. Crie em Todos os jogos, sem busca textual ativa.
- Progresso manual: Quero jogar, Em andamento e Concluído. Não representa troféus ou leitura de saves.
- Fonte Noto Sans com minúsculas e acentos; opção de texto maior e modo gravação persistentes.
- Capas locais PNG/JPEG, carregadas em segundo plano, com cache limitado. Nenhuma capa de jogo acompanha o projeto.
- Consulta ao estado do console antes de abrir um título e tentativa de confirmação posterior.
- Migração dos metadados v1 com cópia anterior preservada; exportação e restauração local.

As pastas existem dentro do Estante; os jogos não são movidos nem reorganizados no menu oficial.
PS2 instalado como PS4 exige classificação manual. O app roda no PS5 sem servidor no PC.
Recentes são pedidos feitos pelo Estante após a consulta inicial, não histórico global nem prova de abertura.

Veja [ESTANTE.md](ESTANTE.md) para controles, dados, instalação e limites e
[DESIGN.md](DESIGN.md) para as decisões de UX.

```sh
bash tools/test-estante.sh
bash tools/preview-estante.sh
```

Os previews `.local/preview-*.png` usam o renderizador real com dados e artes fictícias.
Servem apenas à validação de desenvolvimento; não são um aplicativo para o PC.

## Compilar (Linux / WSL Ubuntu)

```sh
sudo apt-get update
sudo apt-get install clang-18 llvm-18 lld-18 make ninja-build ccache curl wget unzip
make
```

O build baixa dependencias publicas fixadas por versao e hash. Saidas:
`dist/PPSA99841/` e `dist/PPSA99841.zip`.
Saída em pasta para ShadowMount; também pode ser empacotada como imagem `.ffpfsc`. Não é PKG.

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

Autor do Estante: Kauan ([@kndofc](https://instagram.com/kndofc)).

Derivado de [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate),
de BlackBearReloaded, sob GPL-3.0-or-later. Os avisos de autoria foram preservados.
Documentacao original: [docs/UPSTREAM_README.md](docs/UPSTREAM_README.md).

- SDK publico: [ps5-payload-dev/sdk](https://github.com/ps5-payload-dev/sdk), v0.42.
- stb_image: MIT / dominio publico; origem fixada em [third_party/stb/SOURCE.md](third_party/stb/SOURCE.md).
- cJSON 1.7.19: MIT, veja [LICENSE-cJSON](LICENSE-cJSON).
- Licenca do projeto: [LICENSE](LICENSE); demais avisos: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Dependencias baixadas, binarios gerados e dados pessoais do console nao sao versionados.
