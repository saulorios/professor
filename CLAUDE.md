# Lousa Inteligente — Contexto do Projeto

Aplicação desktop em C++/Qt que simula uma lousa escura escrita com giz, com física
realista de depósito de pó. Um professor virtual (IA) envia comandos semânticos e o
motor local transforma esses comandos em traços animados de giz, como uma mão real.

## Princípio central

A IA decide O QUÊ desenhar. O motor local decide COMO.
Mouse, mesa digitalizadora e IA alimentam exatamente o mesmo pipeline de física.

## Stack

- C++17, Qt 6 (Widgets; OpenGL apenas em etapa futura), CMake.
- Sistema alvo principal: Linux (Ubuntu). Deve compilar também em Windows.
- Sem dependências externas além do Qt, salvo quando explicitamente pedido.

## Arquitetura (5 camadas, uma pasta por camada)

```
IA (via proxy que guarda a API key)
   │  JSON Lines: 1 comando semântico por linha (ver docs/ia-protocol.md)
   ▼
src/protocol/  → parser incremental de JSON Lines, validação, fila de comandos
   ▼
src/scene/     → layout (posições relativas, âncoras, ids), geometria 2D,
                 objetos 3D, projeção e remoção de arestas ocultas
   ▼
src/hand/      → "mão do professor": converte polilinhas e texto (fontes Hershey)
                 em traços temporizados (posição, pressão, tempo)
   ▼
src/physics/   → física do giz: BoardSurface, ChalkStick, DepositBuffer,
                 StrokeEngine, Eraser
   ▼
src/render/    → BoardCanvas: desenha o DepositBuffer na tela
src/ui/        → MainWindow, TitleBar e demais widgets de interface
```

### Regras de dependência (importante)

- Cada camada só conhece a camada imediatamente abaixo dela.
- `physics/` é C++ puro (sem QWidget; tipos Qt básicos como QPointF são permitidos).
  No CMake ela é a biblioteca estática `physics`, que só enxerga o QtCore.
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
- A conversão unidades → pixels acontece apenas na fronteira `hand/` → `physics/`.
- A lousa em pixels mede **1920 × 1080** (12 px por unidade), definida em
  `PhysicsParams`. A `BoardCanvas` exibe essa imagem em 16:9, escalada e
  centralizada no body; o resto do body fica com a cor da UI.

## Física do giz (`src/physics/`)

- `PhysicsParams`: todas as constantes ajustáveis da física (lousa, superfície,
  giz, traço, apagador). `BoardCanvasParams` (em `render/`) guarda as da tela:
  pressão do mouse (0.6), faixa de inclinação da caneta e cores.
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
  recalcula e redesenha só essa região a cada evento.

Controles na lousa: botão esquerdo (ou ponta da caneta) = giz; botão direito =
apagador; `Ctrl+Shift+Delete` limpa a lousa (útil para testes).

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
- [ ] Etapa 2 — Mão virtual + player de .jsonl + formas 2D
- [ ] Etapa 3 — Texto com fontes Hershey
- [ ] Etapa 4 — Motor de layout
- [ ] Etapa 5 — Objetos 3D em perspectiva
- [ ] Etapa 6 — Cliente de rede e IA
- [ ] Etapa 7 — Renderização em OpenGL (opcional)
