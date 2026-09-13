# Proxy da Lousa Inteligente

Servidor mínimo que guarda a chave da API e conversa com a Anthropic em modo
streaming. O aplicativo C++ nunca vê a chave: ele só manda as mensagens e
recebe de volta o texto gerado, em pedaços.

## Instalar

```bash
cd proxy
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

O editor (Pyrefly, Pylance…) precisa apontar para esse `.venv/bin/python`, senão
ele procura `fastapi` e `anthropic` no Python do sistema e reclama que não existem.

## Qual IA responde

Escolha do proxy, na variável `LOUSA_PROVEDOR`. O aplicativo não muda nada.

| `LOUSA_PROVEDOR` | serviço | chave | modelo |
|---|---|---|---|
| `openrouter` | [openrouter.ai](https://openrouter.ai), tem modelos gratuitos | `OPENROUTER_API_KEY` | `LOUSA_MODELO`, obrigatório |
| `anthropic` | API da Anthropic (paga) | `ANTHROPIC_API_KEY` | `LOUSA_MODELO` (padrão `claude-sonnet-5`) |
| `openai` | qualquer endpoint no formato OpenAI: Ollama, LM Studio, Groq, DeepSeek… | `LOUSA_CHAVE` (opcional em servidor local) | `LOUSA_MODELO` + `LOUSA_URL`, obrigatórios |

Sem `LOUSA_PROVEDOR`, vale `openrouter` se houver `OPENROUTER_API_KEY`, senão
`anthropic`.

Os modelos gratuitos do OpenRouter terminam em `:free`
([lista](https://openrouter.ai/models?q=free)) — o id vai inteiro em
`LOUSA_MODELO`, por exemplo `nvidia/nemotron-3-ultra-550b-a55b:free`. Vale
testar mais de um: a aula depende de o modelo obedecer ao JSON Lines do
protocolo, e os menores costumam escorregar (linhas que não são JSON são
ignoradas com aviso no log, e cercas ```` ```json ```` são descartadas).

## A chave

Duas formas, o ambiente ganha da primeira:

1. Arquivo `.env` nesta pasta (não vai para o git):

```bash
cp .env.exemplo .env
$EDITOR .env
```

2. Ou direto no terminal que roda o servidor:

```bash
export LOUSA_PROVEDOR=openrouter
export OPENROUTER_API_KEY=sk-or-v1-...
export LOUSA_MODELO=nvidia/nemotron-3-ultra-550b-a55b:free
```

## Rodar

**O aplicativo sobe o proxy sozinho.** Ao abrir a lousa, se o endereço da IA
for local e nada estiver respondendo em `/saude`, ela executa
`.venv/bin/python -m uvicorn servidor:app` nesta pasta, mostra a saída no Log
(botão "Log" do painel do professor) e encerra o servidor ao fechar. Se você já
rodou o proxy no terminal, a lousa usa esse e não o encerra.

- A pasta é procurada a partir do executável, subindo até achar `proxy/`
  (`build/` fica ao lado dela); `LOUSA_PROXY_DIR` indica outra.
- Sem `.venv`, usa `python3`/`python` do sistema (precisa das dependências).
- `LOUSA_PROXY_AUTO=0` desliga o início automático.
- No Linux, se a lousa for morta sem fechar, o proxy morre junto.

Para rodar à mão:

```bash
.venv/bin/uvicorn servidor:app --port 8000
```

Confira com `curl http://127.0.0.1:8000/saude`: ele mostra o provedor, o modelo
e se a chave foi encontrada.

## Quando a IA não responde

Os modelos gratuitos às vezes ficam sobrecarregados: o OpenRouter aceita o
pedido e fica só mandando "aguarde". O proxy desiste depois de
`LOUSA_ESPERA_SEM_ATIVIDADE` segundos (padrão 45) sem o modelo escrever nem
pensar, e o painel do professor mostra o motivo — normalmente a solução é
tentar de novo ou trocar `LOUSA_MODELO`. Modelos que "pensam" antes de
escrever podem levar mais de um minuto até o primeiro comando: enquanto
pensam, o proxy mantém o aplicativo esperando.

## API

`POST /aula`

```json
{"mensagens": [{"papel": "usuario", "texto": "explique o volume do cubo"}]}
```

`papel` é `usuario` ou `professor` (as respostas anteriores da IA, para
perguntas de acompanhamento). A resposta é `text/plain` em chunked transfer:
JSON Lines conforme `docs/ia-protocol.md`, que o servidor usa como system
prompt. Nada além do texto gerado é repassado.

O aplicativo procura o proxy em `http://127.0.0.1:8000/aula`; para mudar,
defina `LOUSA_PROXY` antes de abrir a lousa.
