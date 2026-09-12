"""Proxy da Lousa Inteligente.

Guarda a chave da API (variável de ambiente ANTHROPIC_API_KEY), recebe as
mensagens do aplicativo, chama a API da Anthropic em modo streaming usando
docs/ia-protocol.md como system prompt e repassa ao aplicativo APENAS o texto
gerado, em pedaços (chunked transfer). A chave nunca sai daqui.

Como rodar:
    export ANTHROPIC_API_KEY=sk-ant-...
    uvicorn servidor:app --port 8000
"""

import os
import sys
from pathlib import Path

import anthropic
from fastapi import FastAPI, HTTPException
from fastapi.responses import StreamingResponse
from pydantic import BaseModel

PASTA = Path(__file__).resolve().parent


def carregar_env(caminho: Path) -> None:
    """Lê um .env simples (CHAVE=valor por linha). O ambiente tem prioridade."""
    if not caminho.exists():
        return
    for linha in caminho.read_text(encoding="utf-8").splitlines():
        linha = linha.strip()
        if not linha or linha.startswith("#") or "=" not in linha:
            continue
        chave, valor = linha.split("=", 1)
        os.environ.setdefault(chave.strip(), valor.strip().strip("\"'"))


carregar_env(PASTA / ".env")

# O protocolo do projeto é o system prompt da professora
PROTOCOLO = PASTA.parent / "docs" / "ia-protocol.md"
MODELO = os.environ.get("LOUSA_MODELO", "claude-sonnet-5")
MAX_TOKENS = int(os.environ.get("LOUSA_MAX_TOKENS", "4000"))


class Mensagem(BaseModel):
    papel: str  # "usuario" ou "professor"
    texto: str


class Pedido(BaseModel):
    mensagens: list[Mensagem]


app = FastAPI(title="Lousa Inteligente - proxy")
sistema = PROTOCOLO.read_text(encoding="utf-8")
# O cliente só existe se a chave estiver no ambiente (senão o erro é claro)
cliente = anthropic.AsyncAnthropic() if os.environ.get("ANTHROPIC_API_KEY") else None


@app.get("/saude")
def saude():
    """Confere a configuração sem gastar tokens."""
    return {"modelo": MODELO, "chave": cliente is not None, "protocolo_bytes": len(sistema)}


@app.post("/aula")
async def aula(pedido: Pedido):
    if cliente is None:
        raise HTTPException(status_code=500, detail="ANTHROPIC_API_KEY não está definida no ambiente")
    if not pedido.mensagens:
        raise HTTPException(status_code=400, detail="nenhuma mensagem enviada")

    mensagens = [
        {"role": "assistant" if m.papel == "professor" else "user", "content": m.texto}
        for m in pedido.mensagens
    ]

    # A conexão é aberta aqui, antes da resposta começar: assim erros de chave ou
    # de modelo viram um status HTTP claro, e não um fluxo vazio
    gerente = cliente.messages.stream(
        model=MODELO, max_tokens=MAX_TOKENS, system=sistema, messages=mensagens
    )
    try:
        fluxo = await gerente.__aenter__()
    except anthropic.APIStatusError as erro:
        # 502: quem falhou foi a API, não o aplicativo (um 401 aqui faria o Qt
        # pedir autenticação do proxy e esconder a mensagem)
        raise HTTPException(
            status_code=502, detail=f"a API respondeu {erro.status_code}: {erro.message}"
        ) from erro
    except Exception as erro:  # noqa: BLE001 - rede fora do ar, modelo inválido etc.
        raise HTTPException(status_code=502, detail=f"{type(erro).__name__}: {erro}") from erro

    async def gerar():
        try:
            async for texto in fluxo.text_stream:
                yield texto
        except Exception as erro:  # noqa: BLE001 - o cabeçalho já foi enviado; só dá para registrar
            print(f"erro no meio da resposta: {erro}", file=sys.stderr, flush=True)
        finally:
            await gerente.__aexit__(None, None, None)

    # text/plain em pedaços: o aplicativo desenha assim que a primeira linha chega
    return StreamingResponse(gerar(), media_type="text/plain; charset=utf-8")
