# Banco de escrita manual — formato (schemaVersion 1)

O banco guarda **como** cada caractere foi escrito: os strokes (contatos
contínuos do giz), a ordem, o tempo de cada ponto, a pressão quando houver e as
pausas com o giz levantado. Não é uma fonte: nenhum glifo é gerado a partir de
fonte; cada variante é uma captura real feita no gravador (F7).

## Pastas

```
<raiz>/                     padrão: <pasta do executável>/handwriting
│                           (ou a variável de ambiente LOUSA_ESCRITA)
├── 0041/                   código Unicode do caractere ("A")
│   ├── A01.json            uma variante = um arquivo
│   └── A02.json
├── 0061/                   "a" — pasta própria, sem colisão com "A" em
│   └── a01.json            sistemas de arquivos que ignoram maiúsculas
└── 00E7/                   "ç"
    └── U00E701.json        fora de A-Z/a-z/0-9 o rótulo é "U" + código
```

Caracteres com mais de um código (letra + acento combinante) usam os códigos
unidos por `_` na pasta (`0065_0301`). Cada variante nova recebe o próximo
número livre, olhando a memória **e** a pasta, e é gravada com abertura
exclusiva: um arquivo existente nunca é sobrescrito.

## Espaço do glifo

- 1 unidade = altura da guia de captura (linha de base → linha das maiúsculas).
  Não depende da resolução nem do tamanho da janela de captura.
- Y para baixo (como o resto do projeto). **Linha de base em y = 0**: as
  maiúsculas chegam a y ≈ −1, minúsculas a ≈ −0,55, descendentes ficam em y > 0.
- x = 0 na borda esquerda do traçado.
- Relação com os pontos crus: `points = (raw − capture.originPx) / capture.unitPx`.

## Arquivo

```jsonc
{
  "format": "lousa-handwriting-glyph",
  "schemaVersion": 1,
  "character": "A",
  "variantId": "A01",
  "pointFormat": ["x", "y", "t", "pressure", "tilt"],
  "capture": {
    "pointer": "mouse",            // mouse | pen | touch | unknown
    "unitPx": 240.66,              // px por unidade do glifo
    "originPx": [251, 389.64],     // px que virou (0, 0)
    "areaPx": [742, 573],
    "recordedAt": "2026-09-12T22:37:48.027Z"
  },
  "metrics": {                     // informativo: recalculado ao carregar
    "bounds": [x, y, largura, altura],
    "baseline": 0,
    "start": [x, y], "end": [x, y],
    "durationMs": 1069.2,          // primeiro toque → última levantada
    "inkMs": 234.9,                // giz encostado
    "penUpMs": 834.3               // giz levantado entre strokes
  },
  "strokes": [
    {
      "id": 0,                     // ordem de escrita
      "startMs": 0,                // relativo ao primeiro toque do glifo
      "metrics": {                 // informativo
        "start": [x, y], "end": [x, y], "bounds": [x, y, w, h],
        "length": 1.115, "durationMs": 83.9,
        "direction": -1.107,       // rad, início → fim, Y para baixo
        "initialDirection": -1.107 // rad, direção de entrada do traço
      },
      "points": [[0, -0.0027, 0], [0.02, -0.04, 8.36, 0.61], ...],
      "raw":    [[251, 389, 0],   [256, 380, 8.36, 0.61], ...]
    }
  ]
}
```

- Ponto: `[x, y, t]`, `[x, y, t, pressão]` ou `[x, y, t, pressão|null, inclinação]`.
  `t` em ms desde o início do stroke. Pressão 0..1; inclinação em graus a partir
  da vertical. Ausente = o dispositivo não informou (o mouse não tem pressão).
- `points` é a referência que o motor consome; `raw` é a captura intocada, em
  px, para estudos do movimento. Nenhum dos dois é suavizado.
- Todo stroke é **pen down**. O **pen up** é o intervalo entre o fim de um
  stroke (`startMs + duração`) e o `startMs` do seguinte.
- Velocidades por ponto não são gravadas: saem dos pontos ao carregar.

## Versões

`schemaVersion` sobe quando um campo muda de significado. Um leitor recusa
arquivos de versão maior que a sua; campos novos opcionais não mudam a versão.

## Saída para o motor: `WritingTrajectory`

`HandwritingEngine::generate(texto, opções)` e `appendGlyph(trajetória,
variante, GlyphPlacement, início)` lêem as variantes **sem alterá-las** e
produzem, em unidades da lousa:

- `points`: posição, tempo, pressão, inclinação, velocidade e direção de cada
  amostra com o giz encostado;
- `segments`: trechos `Down` (faixa de `points`, id do stroke) e `Up` (de onde
  para onde e quando, sem amostras — o voo é do HumanMotionEngine);
- `glyphs`: caractere, variante, caixa, origem, avanço, início e fim.

`GlyphPlacement` (escala, rotação, deslocamento da linha de base, velocidade,
pressão) é onde o VariationEngine vai atuar; `WritingProfile` guarda as
amplitudes dessa variação e ainda não é aplicado.
