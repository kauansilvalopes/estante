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
