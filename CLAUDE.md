# Lousa Inteligente — Contexto do Projeto

Aplicação desktop em C++/Qt que simula uma lousa escura escrita com giz, com física
realista de depósito de pó. Um professor virtual (IA) envia comandos semânticos e o
motor local transforma esses comandos em traços animados de giz, como uma mão real.

## Princípio central

A IA decide O QUÊ desenhar. O motor local decide COMO.
Mouse, mesa digitalizadora e IA alimentam exatamente o mesmo pipeline de física.

## Stack

- C++17, Qt 6 (Widgets; OpenGL apenas em etapa futura), CMake.
- Sistema alvo principal: Linux (Ubuntu). Deve compilar também em Windows
  (os fontes são UTF-8; no MSVC o CMake passa `/utf-8`).
- Sem dependências externas além do Qt, salvo quando explicitamente pedido.

## Arquitetura (5 camadas, uma pasta por camada)

```
IA (via proxy que guarda a API key)
   │  JSON Lines: 1 comando semântico por linha (ver docs/ia-protocol.md)
   ▼
src/protocol/  → parser incremental de JSON Lines, validação, fila de comandos
   ▼
src/scene/     → layout (posições relativas, âncoras, ids), geometria 2D,
                 texto (fontes Hershey), objetos 3D, projeção e arestas ocultas
   ▼
src/hand/      → "mão do professor": converte polilinhas (formas e letras)
                 em traços temporizados (posição, pressão, tempo)
   ▼
src/physics/   → física do giz: Board, BoardSurface, ChalkStick, DepositBuffer,
                 StrokeEngine, Eraser
   ▼
src/render/    → BoardCanvas: desenha o DepositBuffer na tela
src/ui/        → MainWindow, TitleBar, PlayerBar, LessonPlayer, TuningPanel e
                 demais widgets
```

### Regras de dependência (importante)

- Cada camada só conhece a camada imediatamente abaixo dela.
- No CMake cada camada é uma biblioteca estática que só enxerga a de baixo:
  `protocol → scene → hand → physics`. `physics` só enxerga o QtCore.
- `physics/` é C++ puro (sem QWidget; tipos Qt básicos como QPointF são permitidos).
- A física recebe SEMPRE o mesmo tipo de entrada, venha do mouse, da mesa
  digitalizadora ou da mão virtual:

```cpp
struct ChalkSample {
    QPointF pos;       // em pixels da lousa
    float   pressure;  // 0..1
    float   tilt;      // 0..1 (0 = giz de ponta, 1 = giz deitado)
    double  timeMs;    // timestamp
};
```

- `scene/` nunca desenha pixels; produz geometria (polilinhas) em unidades da lousa.
- `hand/` nunca decide layout; recebe polilinhas já posicionadas.
- Objetos 3D são projetados para 2D UMA única vez, no momento em que são criados.
  Depois disso viram giz comum na lousa (permanente, sujeito ao apagador).
  Não existe câmera girando nem re-renderização 3D.

## Sistema de coordenadas

- A lousa lógica mede **160 × 90 unidades** (proporção 16:9), origem no canto
  superior esquerdo, Y para baixo.
- Toda a IA e a camada `scene/` trabalham em unidades da lousa.
- A conversão unidades → pixels acontece apenas na fronteira `hand/` → `physics/`
  (`HandParams::pixelsPerUnit`).
- A lousa em pixels mede **1920 × 1080** (12 px por unidade), definida em
  `PhysicsParams`. A `BoardCanvas` exibe essa imagem em 16:9, escalada e
  centralizada no body; o resto do body fica com a cor da UI.
- **Área útil**: 4 unidades para dentro de cada borda; embaixo, o limite é a
  faixa reservada à legenda da fala (`SceneParams::captionBandHeight`, 5
  unidades), e não a borda da lousa. Hoje: x 4–156, y 4–81.

## Física do giz (`src/physics/`)

- `Board`: estado compartilhado (parâmetros, superfície, depósito e giz). Mouse e
  mão virtual escrevem no mesmo `Board`, cada um com o seu `StrokeEngine`/`Eraser`,
  que leem os parâmetros do `Board` a cada uso.
- `PhysicsParams`: todas as constantes ajustáveis da física (lousa, superfície,
  giz, traço, apagador). `BoardCanvasParams` (em `render/`) guarda as da tela:
  pressão do mouse (0.6), faixa de inclinação da caneta, cores e o modo de depuração.
- `BoardSurface`: height map 0..1 com value noise em 2 oitavas (grão de ~2 px +
  ondulação de ~40 px), seed fixa e curva `surfaceGamma`.
- `StrokeEngine`: velocidade suavizada; sub-passos a cada 0.5 px (interpolando
  posição, pressão e tilt); depósito por pixel sob a ponta:
  `contato = pressão − altura`; `qtd = contato · 1/(1 + v·kVelocity) · eficiência ·
  (0.7 + 0.3·ruído)`; `depósito += qtd · (1 − depósito)`. O ruído é um hash da
  posição (`Noise.h`), nunca `rand()`.
- `ChalkStick`: ponta de 3 px que cresce levemente com o desgaste (limite 4.5 px);
  o tilt a transforma numa elipse alongada na direção do traço. `reset()` volta
  ao giz novo.
- `Eraser`: raio de 20 px; remove 85% do depósito **uma vez por passada** e
  espalha parte do resto para os vizinhos (fantasma). Uma nova passada começa a
  cada toque e quando o apagador inverte o sentido (vai e volta).
- `DepositBuffer`: `std::vector<float>` por pixel com dirty rect; a `BoardCanvas`
  recalcula e redesenha só essa região (`refresh()`).

Controles na lousa: botão esquerdo (ou ponta da caneta) = giz; botão direito =
apagador; `Ctrl+Shift+Delete` limpa a lousa (útil para testes); F10 = painel de
ajuste; F12 = modo de depuração do layout.

## Painel de ajuste (F10)

- `ui/TuningPanel`: painel oculto encostado à direita da lousa, abre e fecha com
  F10. Um slider por campo de `PhysicsParams` e `HandParams`, com o valor atual
  (o nome do campo aparece no tooltip). As mudanças valem na hora para os
  próximos traços: `StrokeEngine`/`Eraser` leem os parâmetros do `Board`
  (`Board::setParams`) e a mão usa `VirtualHand::setParams` a partir do próximo
  comando. Os campos da superfície regeneram o height map e só são aplicados ao
  soltar o slider. As fórmulas da física não mudam.
- Campos estruturais (`boardWidth`, `boardHeight`, `pixelsPerUnit`) aparecem
  desabilitados: a resolução da lousa e a conversão 160 × 90 são fixas.
- Botões: "Limpar lousa", "Restaurar padrões" e "Salvar", que grava
  `params.json` na pasta do executável (`{"physics": {...}, "hand": {...}}`,
  chaves = nomes dos campos). Ao iniciar, se o arquivo existir, os valores são
  carregados dele (limitados às faixas dos sliders).

## Aula a partir de arquivo `.jsonl` (sem IA e sem rede)

Fluxo: arquivo → `CommandParser` → `CommandQueue` → `Scene` → `VirtualHand` → física.
`LessonPlayer` (em `ui/`) liga as peças; a `BoardCanvas` recompõe a cada tick da mão.

- `protocol/CommandParser`: `appendData()` recebe pedaços (podem cortar linhas),
  separa por linha e faz parse com `QJsonDocument`; linhas inválidas geram aviso
  no log e são ignoradas. `finish()` processa a última linha sem `\n`.
- `protocol/CommandQueue`: um comando por vez; só avança quando o anterior terminou
  de ser desenhado. `fala` não bloqueia; `pausa` respeita a velocidade.
- Comandos implementados: `forma` (circulo, elipse, retangulo, triangulo, poligono,
  linha, seta, arco), `escrever`, `conectar`, `destacar`, `pausa`, `fala`,
  `apagar`, `limpar`. Os demais geram aviso e são pulados.
- `scene/Scene`: guarda todos os elementos desenhados (com ou sem id) com a
  bounding box e o ponto de referência; `conectar` liga as bordas dos elementos
  (elipse inscrita na bounding box para círculos/elipses, a própria caixa para os
  demais), nunca os centros; `destacar` desenha `sublinhar` (linha sob a caixa),
  `circular` (elipse que passa pelos cantos da caixa) ou `caixa` (retângulo), com
  a folga `highlightGap`. `scene/Geometry2D`: formas → polilinhas; círculos/arcos
  com amostragem adaptativa (`curveTolerance`); tracejado e pontilhado quebram as
  polilinhas. Constantes em `SceneParams`.
- `hand/VirtualHand`: monta uma linha do tempo por comando e a reproduz num QTimer
  de ~60 Hz: acelera no início, freia em curvas fechadas e no fim; pressão sobe
  no toque e alivia ao levantar; tremor determinístico de 0.1–0.3 unidade;
  pausa entre traços. `apagar` passa o `Eraser` em zigue-zague sobre a bounding
  box; `limpar` zera a lousa de uma vez. Os timestamps enviados à física são do
  tempo simulado da mão: a aparência não muda com a velocidade de reprodução.
  Constantes em `HandParams`.
- UI: `File > Abrir aula (.jsonl)...`; `PlayerBar` com Play / Pausar / Reiniciar
  e velocidade (0.5x, 1x, 2x, 4x); a `fala` aparece como legenda (`#Caption`)
  na faixa reservada da base da lousa até a próxima fala.
- Exemplos: `examples/formas.jsonl` (formas, conectar, apagar, limpar),
  `examples/texto.jsonl` (título, frases acentuadas e fórmulas) e
  `examples/agua.jsonl` (Exemplo 1 do protocolo: posicionamento relativo).

## Motor de layout (`scene/Layout`)

- Resolve todas as formas de posicionamento da seção 3 do protocolo, em unidades
  da lousa e dentro da área útil:
  - `ancora`: 9 posições na área útil (`base_*` encosta na faixa da legenda).
  - `abaixo_de` / `acima_de` / `direita_de` / `esquerda_de`: encostado na caixa
    da referência a `margem` (padrão `relativeMargin`), com `alinhar`
    `inicio` | `centro` (padrão) | `fim` no outro eixo.
  - `relativo_a` + `angulo` (0 = direita, positivo = anti-horário) + `distancia`:
    o ponto de referência do elemento vai para esse ponto polar a partir do
    ponto de referência da referência.
  - `em_centro_de`: centraliza a caixa dentro da caixa da referência.
  - `em`: o ponto de referência do elemento vai para [x, y].
- Ponto de referência: nas formas, a origem da geometria (centro do círculo, do
  arco, do retângulo; origem dos `pontos` relativos); no texto, o centro do texto.
- Elemento fora da área útil é empurrado para dentro, com aviso no log.
- Colisão com a caixa de outro elemento: desloca na direção do posicionamento
  (abaixo → para baixo, relativo_a → na direção do ângulo, âncora → para dentro)
  até não colidir, no máximo `maxCollisionAttempts` (10) vezes; depois aceita,
  com aviso. `em` e `em_centro_de` não são deslocados (são pedidos explícitos), e
  a referência do `relativo_a` pode ser tocada (ex.: arco com `distancia` 0).
  Linhas, setas, conexões e destaques não contam como obstáculo (a caixa de um
  traço diagonal cobre uma área que ele não ocupa).
- Referência a id inexistente: usa a âncora `centro`, com aviso no log.
- Modo de depuração (F12): a `BoardCanvas` desenha por cima da lousa, com
  QPainter e fora do `DepositBuffer`, a área útil (tracejada) e as bounding boxes
  com os ids (ou uma descrição, para elementos sem id).

## Texto (comando `escrever`)

- Fontes Hershey de traço único em `resources/fonts/` (embutidas pelo `.qrc`):
  `rowmans.jhf` (Roman Simplex, `"fonte":"normal"`, padrão) e `scripts.jhf`
  (Script Simplex, `"fonte":"cursiva"`, para títulos e destaques; fórmulas sempre
  em normal). A cursiva mantém a ordem original dos traços (já é a da escrita),
  usa `cursiveTracking` e não usa os pares de kerning da normal. A nota de uso
  original (`hershey.txt`) e a origem dos arquivos (`FONTES.md`) acompanham os
  dados. Nada de TTF: o giz risca, não preenche contornos.
- `scene/HersheyFont`: lê o `.jhf` e converte cada caractere em polilinhas. Na
  carga, normaliza a ordem dos traços para a mão (de cima para baixo, da esquerda
  para a direita; traços abertos começam pela ponta de cima/esquerda). Letras
  acentuadas que a fonte não tem (á à â ã é ê í ó ô õ ú ç e maiúsculas) são
  compostas com letra base + diacrítico, e o acento é traçado depois da letra.
  Também compõe `°` e `·`. Métricas (topo das maiúsculas/minúsculas, linha de
  base) são medidas nos próprios glifos.
- `scene/TextLayout`: `tamanho` = altura das maiúsculas (padrão 4); espaçamento
  pelos limites do glifo, `tracking` e kerning simples por pares (AV, To, Y. …);
  índice e expoente com `_` e `^` no próximo caractere ou no grupo `{…}`
  (H_2O, x^2, e^{-x}): 60% do tamanho, deslocados para baixo/para cima.
  Constantes em `SceneParams` (bloco "Texto").
- A mão escreve com `Motion::Writing`: um pouco mais rápida que nas formas
  (`writingSpeed`) e com levantada de giz menor entre traços (`writingPenLiftMs`).

## Paleta

- UI (topbar e body): fundo `#1F1F1F`, separador `#2B2B2B`, texto `#CCCCCC`.
- Lousa: `#1E2621` com variação sutil vinda do height map.
- Giz: `#F2F0E6`.

## Convenções

- Comentários e mensagens de interface em português.
- Nomes de classes, métodos e variáveis em inglês.
- Constantes ajustáveis agrupadas em structs de parâmetros (ex: `PhysicsParams`,
  `HandParams`), nunca espalhadas como números mágicos.
- Nada de alocações no heap dentro de loops por pixel ou por sub-passo de traço.
- Não adicionar funcionalidades além do que foi pedido na etapa atual.
- Ao terminar uma tarefa: compilar, rodar, corrigir erros e atualizar a seção
  "Estado atual" deste arquivo.
- Ao concluir cada etapa com os testes passando, faça commit automaticamente.

## Como compilar

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/lousa
```

Sem `CMAKE_BUILD_TYPE`, o CMake já usa Release (a física roda por pixel).

## Arquivos de referência

- `docs/ia-protocol.md` — system prompt da IA professora e especificação completa
  dos comandos JSON Lines. É a fonte da verdade do protocolo: o parser deve
  aceitar exatamente o que está descrito lá.
- `examples/*.jsonl` — aulas escritas à mão para testar o motor sem usar a IA.

## Estado atual

- [x] Etapa 0 — Janela com TitleBar customizada
- [x] Etapa 1 — Física do giz com mouse
- [x] Etapa 2 — Mão virtual + player de .jsonl + formas 2D
- [x] Etapa 3 — Texto com fontes Hershey
- [x] Etapa 4 — Motor de layout
- [ ] Etapa 5 — Objetos 3D em perspectiva
- [ ] Etapa 6 — Cliente de rede e IA
- [ ] Etapa 7 — Renderização em OpenGL (opcional)
