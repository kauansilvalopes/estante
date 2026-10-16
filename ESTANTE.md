# Estante ? prototipo nativo de PS5

Aplicativo em pasta para ShadowMount. Identificador: PPSA99841.
Nao modifica as pastas do menu oficial: organiza colecoes dentro do Estante.
Nao move, apaga nem reinstala jogos. PS2 portado como PS4 requer classificacao manual.

## Uso
- Direcional: navegar; esquerda: colecoes; direita: titulos.
- X: abrir colecao ou confirmar pedido de iniciar jogo.
- Quadrado: selecionar varios titulos.
- Triangulo / Options: organizar, classificar, criar pastas, favoritos e atualizar.
- L1 / R1: paginas; circulo: voltar; botao PS: sair pelo sistema.
- Teclado de pastas: X digita, quadrado apaga, Options salva.

## Dados e dependencias
Metadados: /download0/estante.json; backup: /download0/estante-backup.json.
Catalogo consultado no Elf Arsenal em 127.0.0.1:6969/appdb.
Pedido de iniciar jogo: /launch?titleId=ID. HTTP 2xx nao comprova que o jogo abriu.
A versao publica inclui catalogo vazio; os titulos sao carregados pela API local.
Limites: 1024 titulos e 32 pastas; nomes de pastas com letras latinas sem acentos.
O executavel funciona no console, sem servidor no PC.

## Validacao
Testes host da leitura de catalogo, persistencia JSON e arvore de pastas.
Compatibilidade de VideoOut, Pad, /download0, rede e lancamento no firmware 11.60
precisa ser validada no aparelho. Nao presumir sucesso a partir da compilacao.

## Creditos
Base: blackbearreloaded/ps5-native-app-boilerplate (GPL-3.0-or-later).
cJSON 1.7.19 (MIT; LICENSE-cJSON). SDK publico ps5-payload-dev v0.42.
