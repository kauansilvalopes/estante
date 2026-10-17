# UX 0.3 — 2026-10-17

A página inicial ajuda a escolher o próximo jogo; a Biblioteca concentra a consulta completa.
A referência de navegação é o fluxo de [Steam Big Picture](https://store.steampowered.com/bigpicture):
entrada pelo controle, foco explícito e acesso rápido à biblioteca. Não são copiados assets da Steam.

O Início mostra escolhas feitas pelo próprio Estante e itens fixados. Sem histórico, usa
fixados, Quero jogar ou catálogo, identificado como descoberta. Coleções têm mosaicos
e hierarquia própria. A Biblioteca tem busca, filtros, seleção múltipla e ordenação.

A navegação usa quatro áreas estáveis por L1/R1; ações de jogos ficam nos detalhes ou
no menu contextual. Ocultar e remover de coleção não apagam jogos. Exclusão de coleção
e restauração de organização exigem confirmação. Preferências visuais são persistidas.

Tipografia com acentos, contraste alto, foco com contorno e opção de texto maior.
Capas ausentes recebem placeholder; a interface não depende de um serviço externo de arte.
Sem animação contínua nesta versão: primeiro validar o custo do renderizador por CPU.

Previews usam títulos e imagens fictícias. A aprovação visual não comprova o desempenho
ou o lançamento no PS5. A versão 0.3 permanece de desenvolvimento até validação física.

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
