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
IA (via proxy/ , que guarda a API key)
   │  JSON Lines: 1 comando semântico por linha (ver docs/ia-protocol.md)
   ▼
src/protocol/  → cliente da IA (AiClient), parser incremental de JSON Lines,
                 validação, fila de comandos
   ▼
src/scene/     → layout (posições relativas, âncoras, ids), geometria 2D,
                 texto (fontes Hershey), sólidos 3D (solids/), projeção e
                 arestas ocultas
   ▼
src/hand/      → "mão do professor": converte polilinhas (formas e letras)
                 em traços temporizados (posição, pressão, tempo)
   ▼
src/physics/   → física do giz: Board, BoardSurface, ChalkStick, DepositBuffer,
                 StrokeEngine, Eraser
   ▼
src/render/    → BoardCanvas: desenha o DepositBuffer na tela e, por cima dele,
                 o giz da mão virtual (ChalkOverlay)
src/ui/        → MainWindow, TitleBar, PlayerBar, AskBar, LessonPlayer,
                 LessonRecorder, LessonEditor, TuningPanel e demais widgets
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

## Sistema de coordenadas: canvas e tela

Dois conceitos distintos:

- **Canvas**: a superfície escrita. Tem **160 unidades de largura** e a altura
  **cresce para baixo**, em blocos de uma tela, sem limite prático além da
  memória. Origem (0,0) no canto superior esquerdo, Y para baixo.
- **Tela (viewport)**: a janela de **160 × 90 unidades** que o usuário vê, com
  deslocamento vertical (`BoardCanvas::scroll()`, em pixels do canvas).

- Toda a IA e a camada `scene/` trabalham em unidades do canvas.
- A conversão unidades → pixels acontece apenas na fronteira `hand/` → `physics/`
  (`HandParams::pixelsPerUnit`). Uma tela em pixels mede **1920 × 1080** (12 px
  por unidade), em `PhysicsParams`; o canvas cresce em blocos de 1080 px.
- `DepositBuffer` e `BoardSurface` cobrem o canvas inteiro e crescem sob demanda
  (`ensureHeight`, sempre entre traços — o crescimento realoca os buffers). O
  height map é gerado pela coordenada **absoluta** do pixel, então o grão da
  lousa não muda de aparência ao rolar. Custo com 5 telas: ~40 MB de depósito e
  ~40 MB de relevo.
- A `BoardCanvas` compõe e mostra **apenas a faixa visível**.
- **Área útil de cada tela**: 4 unidades para dentro das bordas — x 4–156 e,
  na tela *n*, y de `90n+4` a `90n+86`. A faixa da legenda é overlay da tela,
  não faz parte do canvas e **não ocupa área útil**.

### Rolagem

- Usuário: roda do mouse, Page Up/Down, Home, End e a barra fina à direita.
  É imediata e faz o motor parar de arrastar a vista.
- Motor: **animada com easing**, 600 ms por tela, como um professor girando uma
  lousa de rolo (`Scene::ensureVisible` → `BoardCanvas::followTo`).
- Um indicador discreto ("2/3") mostra em que parte da lousa se está.
- Se o usuário rolou por conta própria e a escrita continua fora da vista,
  aparece um aviso clicável ("continuando abaixo ↓") em vez de arrastá-la à
  força; clicar devolve o controle ao motor. Se ele não tocou em nada, a vista
  segue a escrita sozinha. Começar uma aula devolve o controle ao motor.
- O mouse e a caneta desenham na posição do **canvas**, não na da tela.

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
apagador; `Ctrl+Shift+Delete` limpa a lousa (útil para testes); F7 = gravador
de escrita manual; F9 = editor da aula; F10 = painel de ajuste; F12 = modo de
depuração do layout.

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
  linha, seta, arco), `escrever`, `conectar`, `destacar`, `objeto_3d`, `rotular`,
  `cotar`, `traco_livre`, `linha`, `coluna`, `nova_tela`, `pausa`, `fala`,
  `fim_passo`, `apagar`, `limpar`. Os demais geram aviso e são pulados.
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
  `examples/texto.jsonl` (título, frases acentuadas e fórmulas),
  `examples/agua.jsonl` (Exemplo 1: posicionamento relativo),
  `examples/logaritmo.jsonl` (Exemplo 4: aula inteira em fluxo, com duas colunas
  e `nova_tela`), `examples/estresse.jsonl` (60 comandos sem posicionamento
  nenhum: é o teste da regra de não sobrepor),
  `examples/cubo.jsonl` e `examples/casa.jsonl` (Exemplos 2 e 3) e
  `examples/solidos.jsonl` (os seis sólidos em cada uma das quatro vistas).

## Motor de layout (`scene/Layout`)

**Sobreposição nunca é aceitável. Se não couber, a lousa rola.**

### O padrão é o fluxo

Comando sem nenhum campo de posicionamento entra em **fluxo tipo documento**:
abaixo do anterior, com `flowSpacing` (2 u) de espaço, alinhado à esquerda da
coluna atual — títulos (`escrever` com `tamanho` ≥ `flowTitleSize`) saem
centralizados. Quando não cabe mais na tela, o canvas cresce, a vista rola e o
fluxo continua no topo da área nova. Comandos de fluxo: `linha` (pula uma
linha), `coluna` (`esquerda`/`direita`/`unica`, duas colunas por tela) e
`nova_tela`. Elementos mais largos que a coluna ocupam a largura inteira. Se a
aula escolheu a coluna, uma coluna cheia continua **na mesma coluna**, na tela
seguinte — o motor não troca de coluna por conta própria.

### Anticolisão rígida

- `scene/Occupancy`: grade de 1 unidade por célula com a ocupação do canvas — a
  "visão espacial" do motor. É refeita a partir dos elementos a cada colocação e
  consultada por somas acumuladas, então testar uma caixa custa O(1).
  Linhas, setas, conexões, destaques e traços do mouse **não** são obstáculos de
  caixa, mas marcam a grade ao longo do próprio traço (`strokeThickness`), para
  que nenhum texto caia em cima deles.
- **Cinto de segurança**: antes de mais nada, um elemento maior que a área útil
  é reduzido até caber, com aviso — vale para texto, formas, objetos 3D, cotas,
  rótulos e destaques. A cota passa pelo layout como um bloco rígido (continua
  paralela à aresta, mas não sai da área nem cai sobre nada) e o destaque, que
  abraça o alvo, é aparado na área útil.
- Ordem das tentativas, sem exceções: (1) deslocar na direção do posicionamento;
  (2) espaço livre mais próximo na tela atual; (3) tela limpa adiante (o canvas
  cresce); (4) só então reduzir a escala do elemento, com aviso. **Não existe
  mais** o "aceita com aviso depois de 10 tentativas".
- `em` é apenas uma sugestão: se colidir, é deslocado como qualquer outro.
  `em_centro_de` pode sobrepor **só** o elemento que referencia (a letra dentro
  do círculo); `relativo_a` idem (o arco com `distancia` 0 em volta da
  referência). Contra qualquer outro elemento, ambos são deslocados.

### As demais formas de posicionamento

- Resolve as formas da seção 3 do protocolo, em unidades do canvas:
  - `ancora`: 9 posições na área útil da tela atual.
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
- Elemento fora da área útil da tela é empurrado para dentro.
- Referência a id inexistente: usa a âncora `centro`, com aviso no log.
- Modo de depuração (F12): a `BoardCanvas` desenha por cima da lousa, com
  QPainter e fora do `DepositBuffer`, a área útil (tracejada) e as bounding boxes
  com os ids (ou uma descrição, para elementos sem id).

## Giz visível (`render/ChalkOverlay`)

- A mão virtual publica onde o giz está (`ChalkPose`: ponta em pixels da lousa,
  direção do traço, quanto está levantado). A `BoardCanvas` desenha o giz com
  QPainter **por cima** da lousa, fora do `DepositBuffer`: não deixa rastro.
- Aparência: cilindro em perspectiva simples (corpo `#F2F0E6`, face lateral mais
  escura, base em elipse achatada e ponta gasta irregular), ~3 unidades, sem
  contorno e sem sombra dura. A ponta fica exatamente na amostra atual da mão.
- O giz aponta na direção do traço, inclinado para trás (`tiltDegrees`, 35°), e
  gira suavemente ao mudar de direção (`HandParams::chalkTurnRate`, nunca salto).
  Entre traços ele sobe (`liftHeight`) e clareia (`liftFade`), descendo de novo
  ao encostar. Ao pausar ou terminar, some com fade de `fadeMs` (300 ms).
- Sombra na lousa é opcional (`shadow`, padrão desligada). Ligar/desligar o giz:
  View > "Mostrar giz" (padrão ligado) ou o slider do F10 — são o mesmo valor.
- Desenhando com o mouse o giz **não** aparece: só a mão virtual publica pose.

## Escrita humana (`scene/Humanizer`)

Aplicado às polilinhas do glifo antes de entregar à mão, sem trocar de fonte.
`HumanizerParams::intensity` (0..1, padrão 0.5) é o mestre e escala todo o
resto; com 0 a saída é exatamente a da fonte (regressão).

- Cada ocorrência de cada caractere tem a sua seed (caractere + posição na frase
  + seed da aula): o mesmo "a" sai diferente nas duas vezes.
- Escala ±4% e rotação ±2,5° por letra, em torno da base dela; linha de base com
  ±0,15 u vinda de ruído 1D ao longo da frase (ondulada, não serrilhada).
- Segmentos retos longos viram curvas de flecha até 1,5% (é o que mais tira a
  cara de plotter); quinas dentro do traço ficam levemente arredondadas.
- Traços fechados (O, o, D, 0) não fecham perfeito: falha ou sobreposição de até
  2% do perímetro. Traços que encostam em outro passam até 2% além (o "T").
- Ritmo: ±10% de velocidade entre letras, micro-pausas depois de vírgula, ponto
  e entre palavras, e um traço vez por outra mais rápido e mais leve
  (`HandStroke` leva velocidade, pressão e pausa próprios até a mão).
- Vale para a fonte normal e para a cursiva; na cursiva a intensidade cai pela
  metade (`cursiveScale`) e a ordem dos traços não muda.
- Ordem dos traços: `HersheyFont` usa uma tabela de ordem convencional de letra
  de forma (A-Z, a-z, 0-9) — A: diagonal esquerda, diagonal direita, barra;
  E: haste e os três horizontais; M: haste, desce, sobe, haste; O: de cima, no
  sentido anti-horário; t: haste e depois a barra; i: haste e depois o pingo.
  Onde a tabela não chega (pontuação, símbolos), vale a ordenação genérica de
  antes. Acentos continuam por último.

## Gravar e editar aulas

- File > "Gravar traços" (Ctrl+R) grava o que for desenhado com o mouse ou a
  caneta; um indicador discreto aparece no canto da lousa. `ui/LessonRecorder`
  transforma cada traço em um comando `traco_livre`, com os pontos em unidades
  do **canvas** (a rolagem já somada: gravar com a lousa rolada guarda a posição
  certa) simplificados por Douglas-Peucker (tolerância 0,2 u) e guardando a
  pressão e o instante de cada ponto que sobrou. Ao reproduzir, a lousa cresce
  até caber o traço (`Layout::include`) e a vista rola até ele.
- `traco_livre` é do protocolo (documentado em `docs/ia-protocol.md` com a nota
  de que a IA não deve gerá-lo). Ao reproduzir, `VirtualHand::drawRecorded`
  refaz o traço com o tempo e a pressão originais: sem perfil de velocidade,
  sem tremor e sem reamostragem.
- File > "Salvar aula (.jsonl)..." (Ctrl+S) grava a aula inteira.
- `ui/LessonEditor` (F9): a aula em JSON Lines, fonte monoespaçada.
  "Aplicar e reproduzir" (Ctrl+Enter com o foco no editor) valida antes de
  rodar — erros aparecem como "linha N: ..." e a aula atual não é tocada.
  "Inserir comando" traz um modelo pronto de cada tipo; Abrir e Salvar usam os
  mesmos diálogos do menu File. Assim dá para desenhar, gravar e depois
  acrescentar falas e pausas no texto.

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
- **Nada ultrapassa a largura da coluna.** O texto quebra em linhas, preferindo,
  nesta ordem: o espaço entre palavras; um operador (⇔, =, +, −, ·, /), que fica
  no fim da linha e é repetido no começo da seguinte, como se faz em matemática;
  e, em último caso, o hífen entre letras. Índices, expoentes e grupos `{…}`
  nunca são partidos. Uma palavra indivisível maior que a coluna é reduzida, com
  aviso. As linhas alinham pela primeira e o entrelinhamento é `lineSpacing`
  (1,4 × a altura da letra); a mão respira ao mudar de linha
  (`HumanizerParams::pauseLine`). O elemento continua sendo UM só, com a caixa
  cobrindo todas as linhas, e `destacar sublinhar` sublinha uma a uma.
- Símbolos que a fonte Hershey não traz (⇔, ⇒, ≤, ≥, ≠, √, π, Δ, Σ, ∫, ∞, ±, ×, ∈)
  são desenhados pelo `HersheyFont`, como já era feito com ° e ·.
- A mão escreve com `Motion::Writing`: um pouco mais rápida que nas formas
  (`writingSpeed`) e com levantada de giz menor entre traços (`writingPenLiftMs`).

## Aula com IA de verdade (proxy + `protocol/AiClient`)

```
AskBar (pergunta) → AiClient → proxy/ (guarda a chave) → API da Anthropic
                       ↑ texto em pedaços (chunked)
                 CommandParser → CommandQueue → Scene → mão → física
```

- `proxy/servidor.py` (FastAPI + uvicorn, fora do app): `POST /aula` recebe
  `{"mensagens":[{"papel":"usuario|professor","texto":"..."}]}`, chama a IA em
  modo streaming usando `docs/ia-protocol.md` como system prompt e devolve
  **apenas o texto gerado**, em `text/plain` chunked. A chave vem do ambiente
  ou de um `.env` na pasta `proxy/` (fora do git) e **nunca** entra no app C++.
  `GET /saude` confere a configuração sem gastar tokens. Erros da API viram 502
  com `{"detail": ...}` antes de o fluxo começar (401 faria o Qt pedir
  autenticação e esconder a mensagem). Instruções em `proxy/README.md`.
- **Qual IA responde é escolha do proxy** (`LOUSA_PROVEDOR`); o app não muda:
  `anthropic` (biblioteca oficial, `ANTHROPIC_API_KEY`), `openrouter`
  (`OPENROUTER_API_KEY`, tem modelos gratuitos terminados em `:free`) e
  `openai`, que é qualquer endpoint no formato OpenAI — Ollama, LM Studio,
  Groq, DeepSeek — com `LOUSA_URL` e `LOUSA_CHAVE` (opcional em servidor
  local). Os dois últimos usam o mesmo adaptador SSE (`httpx`), que lê
  `choices[0].delta.content`. Sem `LOUSA_PROVEDOR`, vale `openrouter` se houver
  `OPENROUTER_API_KEY`, senão `anthropic`. Outras variáveis: `LOUSA_MODELO`
  (obrigatória fora da Anthropic), `LOUSA_MAX_TOKENS`, `LOUSA_TEMPO_LIMITE`.
- Modelos menores nem sempre obedecem ao protocolo: linhas que não são JSON
  válido já eram ignoradas com aviso, e o `CommandParser` também descarta as
  cercas ```` ```json ```` que eles costumam pôr em volta da resposta.
- `protocol/AiClient`: `QNetworkAccessManager` (assíncrono, sem threads). Cada
  `readyRead` repassa o pedaço cru ao `CommandParser`, que já sabe juntar linhas
  cortadas: o desenho começa na primeira linha completa, sem esperar o fim da
  resposta. Guarda o histórico da conversa (`maxMessages`) para perguntas de
  acompanhamento, tem tempo limite por inatividade (`idleTimeoutMs`), trata erro
  de rede e `stop()` cancela o reply em andamento. Endereço do proxy em
  `AiClientParams::url`, sobreposto pela variável `LOUSA_PROXY`.
- `protocol/ProxyLauncher`: ao abrir o app, se a URL da IA é local e o
  `/saude` não responde, sobe `python -m uvicorn servidor:app` na pasta
  `proxy/` (achada subindo a partir do executável, ou `LOUSA_PROXY_DIR`), com o
  Python do `.venv` se existir. Espera o `/saude` responder, manda a saída do
  uvicorn para o Log e o estado para o painel. Ao fechar, `terminate()` e, se
  preciso, `kill()`; no Linux `PR_SET_PDEATHSIG` encerra o proxy até se o app
  for morto. Proxy já rodando (terminal) é usado e não é encerrado; porta
  ocupada por outro serviço vira erro. `LOUSA_PROXY_AUTO=0` desliga.
  Constantes em `ProxyLauncherParams`. A chave continua só no proxy.
- `ui/AgentPanel`: painel lateral direito (largura 320, splitter, F8 ou
  View > "Painel do professor"). Cabeçalho "Professor" com nova aula (+),
  histórico e fechar; no meio a **timeline**, um cartão por pergunta com o
  resumo do que foi desenhado ("18 comandos · 42 s") e o estado (desenhando,
  concluído, parado, erro); no rodapé o campo de pergunta (cresce até 4 linhas,
  Ctrl+Enter ou a seta envia), os botões Parar, Continuar (só no `fim_passo`) e
  Log, e o aviso de que a IA pode errar. A barra de baixo da janela ficou só com
  os controles de reprodução.
- Cada cartão guarda a faixa de Y do canvas onde a resposta foi desenhada:
  clicar nele rola a lousa até lá (animado) e realça o trecho por 1 s; o cartão
  do trecho à vista fica destacado, inclusive quando se rola a lousa à mão.
- Toda pergunta começa em área limpa (`Scene::startFreshScreen`), então cada
  resposta tem a sua região e a timeline bate com a lousa.
- A timeline é salva junto com a aula: um comando `{"tipo":"pergunta"}` antes
  dos comandos da resposta (uso interno, documentado no protocolo). Ao reabrir
  o `.jsonl`, os cartões e as faixas são reconstruídos.
- `fim_passo` é tratado pela `CommandQueue` (sinal `stepFinished`); o botão
  "Continuar" envia `continue` e a aula segue **sem limpar a lousa**, para a IA
  poder referenciar os ids já desenhados. Uma pergunta nova limpa a lousa.
- `LessonPlayer` ganhou o modo streaming (`startStream`/`appendStreamData`/
  `finishStream`): cada comando vai para a fila assim que a linha chega, e
  "Reiniciar" redesenha o que já veio.

## Objetos 3D (`objeto_3d`, `rotular`, `cotar`)

O objeto é montado como modelo 3D de arestas e faces, projetado para 2D **uma
única vez** e entregue à mão como polilinhas comuns. Não há câmera em tempo real
nem re-renderização: depois de desenhado é giz como qualquer outro.

- `scene/solids/Mesh`: vértices (`QVector3D`), faces convexas (normal para fora
  e referencial de decalque `origin`/`uAxis`/`vAxis`, com `v = 0` na base) e
  arestas únicas com as duas faces vizinhas. Nomes semânticos nas faces
  (frente, tras, esquerda, direita, topo, base) e nos vértices
  (`frente_topo_direita`, …). Eixos do modelo: X à direita, Y para cima, Z para
  longe; cada parte nasce apoiada em `y = 0` e é movida por `posicao`.
- `scene/solids/Solids`: `caixa`, `prisma_triangular`, `piramide` (3 a 8 lados),
  `cilindro`, `cone` e `esfera`. Nos três curvos a malha facetada serve só para
  a oclusão: o desenho usa as silhuetas (elipses das bases e geratrizes de
  contorno; na esfera, o contorno e o equador tracejado).
- `scene/Projection`: `cavaleira` (`x' = x + z·reducao·cos(angulo)`,
  `y' = y + z·reducao·sin(angulo)`), `isometrica` (ortográfica com os eixos a
  30°), `perspectiva_1` e `perspectiva_2` (câmera pinhole com `QMatrix4x4`;
  `rotacao` gira o objeto em torno de Y e `altura_olho` põe o olho abaixo, no
  meio ou acima). A distância da câmera vem do campo de visão (`fieldOfView`,
  ~35°) para não distorcer demais.
- `scene/HiddenLines`: face visível quando a normal aponta para o observador.
  Entre partes, cada aresta é amostrada e cada amostra é testada contra as faces
  visíveis das outras partes (ponto-em-polígono na projeção + profundidade ao
  longo do raio). Faces coladas entre duas partes (a base do telhado sobre o
  topo da caixa) são internas e não contam. Os trechos ocultos são omitidos ou
  tracejados conforme `arestas_ocultas` (padrão: tracejar nas vistas paralelas,
  omitir nas perspectivas).
- `scene/Object3D`: junta tudo. Decalques são mapeados na face em 3D antes de
  projetar (portas e janelas acompanham a perspectiva) e decalque em face oculta
  não é desenhado. Arestas colineares contíguas de partes diferentes são fundidas
  e desenhadas uma vez só. Ordem de desenho: face da frente → arestas de
  profundidade → demais arestas → ocultas tracejadas (pressão leve) → decalques;
  dentro de cada grupo os trechos que se encontram viram um traço contínuo.
- Escala: 1:1 com as unidades da lousa em `cavaleira` e `isometrica` (uma aresta
  de 24 mede 24 na frente do objeto); nas perspectivas a aresta vertical mais
  próxima do observador fica com a altura declarada. Se não couber na área útil,
  a escala é reduzida uniformemente, com aviso. A bounding box 2D é obstáculo
  normal no Layout; arestas, decalques e rótulos internos não colidem entre si.
- `rotular`: texto perto do vértice, empurrado para fora da silhueta.
  `cotar` (`largura`, `altura`, `profundidade`): escolhe uma aresta visível do
  lado de fora, desenha a linha paralela a ela com pequenos traços nas pontas e
  o texto no meio. Ambos contam como obstáculos para os outros elementos.
- Depuração (F12): além das caixas, a `BoardCanvas` mostra as arestas ocultas em
  outra cor (mesmo com `omitir`), os pontos de fuga e as linhas finas até eles.
- Constantes em `SceneParams` (bloco "Objetos 3D").

## Escrita manual (`src/handwriting/`, etapa 1: banco de gestos)

Base para trocar a escrita por fonte por gestos capturados. Pipeline futuro:
`texto → HandwritingEngine → WritingTrajectory → HumanMotionEngine → mão → giz → lousa`.
Esta etapa tem só o banco, o gravador e o contrato da trajetória; mão, braço,
pulso, IK e variação **não** existem ainda. O Hershey continua sendo a fonte do
`escrever` (e será o fallback).

- Biblioteca `handwriting` (CMake), **independente**: só enxerga o QtCore, não
  conhece giz, cena nem mão. `lousa` a usa pelo gravador; `scene/` e o
  HumanMotionEngine vão consumi-la nas próximas etapas.
- `types/HandwritingTypes.h`: `WritingPoint` (pos, t relativo ao stroke,
  pressão/inclinação ou `kUnknown`, velocidade derivada), `Stroke` (id = ordem,
  `startMs` relativo ao glifo, `points` normalizados e `rawPoints` em px, nunca
  suavizados), `GlyphVariant`, `Glyph`, `GlyphMetrics`, `CaptureInfo` e
  `WritingProfile` (só contrato). Todo stroke é pen down; pen up é o intervalo
  entre strokes.
- Espaço do glifo: 1 = altura da guia (linha de base → maiúsculas), Y para
  baixo, **linha de base em y = 0**, x = 0 na borda esquerda.
  `points = (raw − originPx) / unitPx`: independe da resolução.
- `data/GlyphGeometry`: medidas sempre derivadas dos pontos (caixa, início,
  fim, comprimento, duração, direção e direção de entrada, tempo de giz
  encostado/levantado). `data/GlyphSerializer`: JSON com `schemaVersion`
  (formato em `docs/handwriting-format.md`). `data/GlyphDatabase`: pasta por
  código Unicode, ids `A01`, `a01`, `U00E701`; o próximo número olha memória e
  disco, e a gravação usa abertura exclusiva (nunca sobrescreve).
  `removeVariant` apaga o arquivo e tira do banco (a pasta vazia sai junto);
  se era a de maior número, o número volta a ficar livre.
- `recorder/StrokeRecorder`: pointerDown/Move/Up → strokes, desfazer, limpar,
  `build()` da variante. `engine/HandwritingEngine`: `appendGlyph` com
  `GlyphPlacement` (escala, rotação, baseline, velocidade, pressão — sem tocar
  na variante) e `generate(texto)` numa linha, variante escolhida pela seed.
  `types/WritingTrajectory.h`: pontos (pos, t, pressão, velocidade, direção),
  segmentos `Down`/`Up` e caixas dos glifos, em unidades da lousa.
- `ui/HandwritingRecorder` (File > "Gravador de escrita manual...", F7): campo
  do caractere e atalhos A E I O U M N R S, área com guias
  (`ui/HandwritingCapture`: mouse, caneta com pressão/inclinação e toque, com o
  timestamp do evento), Limpar (Delete), Desfazer (Ctrl+Z), Salvar variante
  (Ctrl+S), "Excluir variante" (a selecionada na lista, com confirmação),
  "Mostrar trajetória" (número, cor e seta por stroke, giz levantado
  pontilhado) e "Mostrar pontos"; lista das variantes (clicar carrega) e
  detalhes com caixa, timestamps, ordem e pen up/down. Enquanto a janela está
  aberta a compressão de eventos do mouse fica desligada. Banco em
  `<pasta do executável>/handwriting` ou `LOUSA_ESCRITA`.
- Testes: `tests/handwriting/tst_handwriting.cpp` (QtTest), `ctest --test-dir build`.

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
Os testes (se o Qt6::Test estiver instalado) rodam com `ctest --test-dir build`.

Para a aula com IA, o proxy precisa estar instalado (a chave fica só nele). A
lousa o inicia sozinha ao abrir e o encerra ao fechar; rodá-lo à mão continua
valendo:

```bash
cd proxy
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
cp .env.exemplo .env    # provedor, chave e modelo (ou exporte no terminal)
.venv/bin/uvicorn servidor:app --port 8000
```

## Arquivos de referência

- `docs/ia-protocol.md` — system prompt da IA professora e especificação completa
  dos comandos JSON Lines. É a fonte da verdade do protocolo: o parser deve
  aceitar exatamente o que está descrito lá.
- `examples/*.jsonl` — aulas escritas à mão para testar o motor sem usar a IA.
- `docs/handwriting-format.md` — formato do banco de escrita manual.

## Estado atual

- [x] Etapa 0 — Janela com TitleBar customizada
- [x] Etapa 1 — Física do giz com mouse
- [x] Etapa 2 — Mão virtual + player de .jsonl + formas 2D
- [x] Etapa 3 — Texto com fontes Hershey
- [x] Etapa 4 — Motor de layout
- [x] Etapa 5 — Objetos 3D em perspectiva
- [x] Etapa 6 — Cliente de rede e IA
- [ ] Etapa 7 — Renderização em OpenGL (opcional)
- [x] Escrita manual 1 — Banco de gestos, gravador (F7) e contrato da WritingTrajectory
- [ ] Escrita manual 2 — HandwritingEngine com variação + HumanMotionEngine
