# UX atual — catálogo, detalhes e estante

Início, Biblioteca e Detalhes são contextos diferentes. O Início destaca um jogo com arte larga,
plataforma, dados locais e ações, seguido por fileiras de jogos e coleções. A Biblioteca reúne a
grade completa, busca, filtros e ordenação; Detalhes é uma página inteira. A estante 3D segue
disponível como estilo alternativo do Início. As referências de navegação são o fluxo do
[Orbit Store](https://github.com/saawant12/orbit-store-ps5) e o Steam Big Picture. Não são copiados
código, arte ou marcas.

O tema padrão usa fundo preto, texto branco e azul (#0070D1) para foco e progresso. A fonte Inter
está licenciada pela SIL Open Font License 1.1. Há cinco temas. Só são mostrados dados que o Estante
possui: ele não inventa sinopse, classificação ou tamanho. “Continuar jogando” considera somente
lançamentos iniciados pelo Estante. A arte larga carrega em segundo plano; sem ela, o app usa a capa
quadrada e um degradê de cor.

A interface principal é renderizada na GPU via OpenGL. As prévias no PC usam títulos e imagens
fictícias; validam layout e renderização, mas não comprovam desempenho, instalação, leitura dos
emuladores nem lançamento no PS5. A única validação de firmware informada pelo autor é a 11.60.

---
## Referência histórica da versão anterior

# Estante: biblioteca para TV

Direcao propria inspirada na referencia de interface escura para midia descrita em
https://getdesign.md/elevenlabs/design-md (consultada em 2026-10-16).
Nao reproduz marca, assets ou layout da ElevenLabs. As regras abaixo sao do Estante.

- Fundo carvao frio e superficies discretamente mais claras. Verde suave destaca foco e acoes.
- O jogo e o protagonista: grade de quatro colunas por duas linhas, imagens quadradas completas.
- Barra lateral permanente com colecoes. Direcional move entre jogos; esquerda na borda ou circulo volta as colecoes.
- Foco usa contorno espesso e texto destacado; selecao usa marca X separada de favorito.
- Nome completo do jogo em destaque abaixo da grade; titulos longos abreviados apenas nos cards.
- Imagens instaladas no console, carregadas em segundo plano. Falha mantem card com identidade e titulo.
- Cache de 16 miniaturas de 256 px em memoria; sem baixar ou publicar capas de terceiros.
- Nao depender de hover, mouse, cor isolada ou texto pequeno para concluir uma acao.
- Manter confirmacao antes de lancar e excluir pastas; nunca excluir jogos.

A fonte bitmap atual e uma limitacao do prototipo. Tipografia vetorial e animacoes
ficam para uma etapa posterior, apos medir desempenho e validar o fluxo no console.
