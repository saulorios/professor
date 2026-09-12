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

## A chave

Duas formas, o ambiente ganha da primeira:

1. Arquivo `.env` nesta pasta (não vai para o git):

```bash
cp .env.exemplo .env
$EDITOR .env            # ANTHROPIC_API_KEY=sk-ant-...
```

2. Ou direto no terminal que roda o servidor:

```bash
export ANTHROPIC_API_KEY=sk-ant-...       # obrigatório
export LOUSA_MODELO=claude-sonnet-5       # opcional
export LOUSA_MAX_TOKENS=4000              # opcional
```

## Rodar

```bash
.venv/bin/uvicorn servidor:app --port 8000
```

Confira com `curl http://127.0.0.1:8000/saude`: `"chave": true` quer dizer que o
servidor achou a chave.

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
