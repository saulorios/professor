"""Proxy da Lousa Inteligente.

Guarda a chave da API (variável de ambiente ou .env desta pasta), recebe as
mensagens do aplicativo, chama a IA em modo streaming usando
docs/ia-protocol.md como system prompt e repassa ao aplicativo APENAS o texto
gerado, em pedaços (chunked transfer). A chave nunca sai daqui.

Qual IA responde é escolha do proxy (LOUSA_PROVEDOR); o aplicativo não muda:

    anthropic   API da Anthropic (paga)          ANTHROPIC_API_KEY
    openrouter  openrouter.ai (tem modelos free) OPENROUTER_API_KEY
    openai      qualquer endpoint no formato     LOUSA_CHAVE (opcional)
                OpenAI (Ollama, LM Studio,       LOUSA_URL (obrigatória)
                Groq, DeepSeek...)

Como rodar:
    cp .env.exemplo .env   # e ponha a chave lá
    .venv/bin/uvicorn servidor:app --port 8000
"""

import asyncio
import json
import os
import sys
import time
from pathlib import Path
from typing import AsyncIterator

import anthropic
import httpx
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
MAX_TOKENS = int(os.environ.get("LOUSA_MAX_TOKENS", "4000"))
TEMPO_LIMITE = float(os.environ.get("LOUSA_TEMPO_LIMITE", "300"))
# Quanto tempo o modelo pode ficar sem escrever NEM pensar antes de o proxy
# desistir com um erro claro. Os "aguarde" do OpenRouter não contam: um provedor
# sobrecarregado manda só isso. Fica abaixo dos 60 s sem dados que o app tolera.
ESPERA_SEM_ATIVIDADE = float(os.environ.get("LOUSA_ESPERA_SEM_ATIVIDADE", "45"))
# Enquanto o modelo pensa, uma linha vazia a cada tanto mantém o app esperando
# (o parser do app ignora linhas vazias)
SINAL_DE_VIDA = 10.0

# Sem LOUSA_PROVEDOR, vale o provedor cuja chave estiver definida
PADRAO = "openrouter" if os.environ.get("OPENROUTER_API_KEY") else "anthropic"
PROVEDOR = os.environ.get("LOUSA_PROVEDOR", PADRAO).strip().lower()
CHAVE_ENV = {"anthropic": "ANTHROPIC_API_KEY", "openrouter": "OPENROUTER_API_KEY", "openai": "LOUSA_CHAVE"}
MODELO_PADRAO = {"anthropic": "claude-sonnet-5"}
URL_PADRAO = {"openrouter": "https://openrouter.ai/api/v1/chat/completions"}

CHAVE = os.environ.get(CHAVE_ENV.get(PROVEDOR, "LOUSA_CHAVE"), "")
MODELO = os.environ.get("LOUSA_MODELO", MODELO_PADRAO.get(PROVEDOR, ""))
URL = os.environ.get("LOUSA_URL", URL_PADRAO.get(PROVEDOR, ""))


class Mensagem(BaseModel):
    papel: str  # "usuario" ou "professor"
    texto: str


class Pedido(BaseModel):
    mensagens: list[Mensagem]


app = FastAPI(title="Lousa Inteligente - proxy")
sistema = PROTOCOLO.read_text(encoding="utf-8")
anthropic_cliente = anthropic.AsyncAnthropic() if PROVEDOR == "anthropic" and CHAVE else None
http = httpx.AsyncClient(timeout=httpx.Timeout(30.0, read=TEMPO_LIMITE))


def conferir_configuracao() -> None:
    """Erro claro antes de qualquer chamada, em vez de uma resposta vazia."""
    if PROVEDOR not in CHAVE_ENV:
        raise HTTPException(status_code=500, detail=f"LOUSA_PROVEDOR \"{PROVEDOR}\" não existe "
                                                    f"(anthropic, openrouter ou openai)")
    if not CHAVE and PROVEDOR != "openai":
        raise HTTPException(status_code=500, detail=f"{CHAVE_ENV[PROVEDOR]} não está definida "
                                                    f"(no ambiente ou no .env da pasta proxy/)")
    if not MODELO:
        dica = "veja a lista em https://openrouter.ai/models?q=free" if PROVEDOR == "openrouter" else ""
        raise HTTPException(status_code=500, detail=f"defina LOUSA_MODELO para o provedor {PROVEDOR}. {dica}".strip())
    if PROVEDOR != "anthropic" and not URL:
        raise HTTPException(status_code=500, detail="defina LOUSA_URL com o endpoint /v1/chat/completions")


async def abrir_anthropic(mensagens: list[dict]) -> AsyncIterator[str]:
    """Streaming pela biblioteca oficial da Anthropic."""
    gerente = anthropic_cliente.messages.stream(
        model=MODELO, max_tokens=MAX_TOKENS, system=sistema, messages=mensagens
    )
    fluxo = await gerente.__aenter__()  # a conexão abre aqui: erros viram HTTP, não fluxo vazio

    async def gerar():
        try:
            async for texto in fluxo.text_stream:
                yield texto
        except Exception as erro:  # noqa: BLE001 - o cabeçalho já foi enviado; só dá para registrar
            print(f"erro no meio da resposta: {erro}", file=sys.stderr, flush=True)
        finally:
            await gerente.__aexit__(None, None, None)

    return gerar()


def ler_evento_sse(linha: str) -> tuple[str, str] | None:
    """Uma linha SSE no formato OpenAI → ("texto" | "pensando" | "erro" | "fim", valor) ou None.

    Comentários (": OPENROUTER PROCESSING") e pedaços vazios viram None. O
    raciocínio dos modelos que "pensam" antes de escrever vira "pensando": não
    vai para o app, mas mostra que o modelo está trabalhando.
    """
    if not linha.startswith("data:"):
        return None
    dado = linha[5:].strip()
    if dado == "[DONE]":
        return ("fim", "")
    try:
        objeto = json.loads(dado)
    except json.JSONDecodeError:
        return None
    if erro := objeto.get("error"):
        mensagem = erro.get("message", erro) if isinstance(erro, dict) else erro
        return ("erro", str(mensagem))
    escolhas = objeto.get("choices") or []
    delta = (escolhas[0].get("delta") or {}) if escolhas else {}
    if texto := delta.get("content"):
        return ("texto", texto)
    if delta.get("reasoning") or delta.get("reasoning_content") or delta.get("reasoning_details"):
        return ("pensando", "")
    return None


async def eventos_sse(linhas: AsyncIterator[str]) -> AsyncIterator[tuple[str, str]]:
    """Eventos úteis do fluxo. TimeoutError se passar ESPERA_SEM_ATIVIDADE sem texto nem raciocínio."""
    ultima_atividade = time.monotonic()
    while True:
        restante = ESPERA_SEM_ATIVIDADE - (time.monotonic() - ultima_atividade)
        if restante <= 0:
            raise asyncio.TimeoutError
        try:
            linha = await asyncio.wait_for(linhas.__anext__(), restante)
        except StopAsyncIteration:
            return
        evento = ler_evento_sse(linha)
        if evento is None:
            continue
        if evento[0] in ("texto", "pensando"):
            ultima_atividade = time.monotonic()
        yield evento


def linha_de_erro(mensagem: str) -> str:
    """Erro dentro de uma resposta já começada: uma linha do protocolo que o app
    mostra no painel (uso interno; ver docs/ia-protocol.md)."""
    return json.dumps({"tipo": "erro", "mensagem": mensagem}, ensure_ascii=False) + "\n"


async def abrir_openai(mensagens: list[dict]) -> AsyncIterator[str]:
    """Streaming no formato OpenAI (SSE), usado pelo OpenRouter e compatíveis.

    A resposta HTTP só começa quando o modelo dá sinal de trabalho (texto ou
    raciocínio). Antes disso, erro do provedor (sobrecarga, limite do plano
    gratuito) ou silêncio longo viram HTTP 502/504 com o motivo. Depois, um
    erro vira uma linha {"tipo":"erro"} no próprio fluxo.
    """
    cabecalhos = {"Content-Type": "application/json", "X-Title": "Lousa Inteligente"}
    if CHAVE:
        cabecalhos["Authorization"] = f"Bearer {CHAVE}"
    corpo = {
        "model": MODELO,
        "max_tokens": MAX_TOKENS,
        "stream": True,
        # O protocolo vai como mensagem de sistema, igual ao caminho da Anthropic
        "messages": [{"role": "system", "content": sistema}] + mensagens,
    }

    contexto = http.stream("POST", URL, headers=cabecalhos, json=corpo)
    resposta = await contexto.__aenter__()
    if resposta.status_code != 200:
        detalhe = (await resposta.aread()).decode("utf-8", "replace").strip()
        await contexto.__aexit__(None, None, None)
        raise HTTPException(status_code=502, detail=f"a API respondeu {resposta.status_code}: {detalhe}")

    eventos = eventos_sse(resposta.aiter_lines())
    dica = "Tente de novo ou troque LOUSA_MODELO."
    try:
        primeiro = await eventos.__anext__()
    except (asyncio.TimeoutError, StopAsyncIteration, httpx.HTTPError) as erro:
        await contexto.__aexit__(None, None, None)
        if isinstance(erro, asyncio.TimeoutError):
            raise HTTPException(status_code=504, detail=f"o modelo {MODELO} não começou a responder em "
                                                        f"{ESPERA_SEM_ATIVIDADE:.0f} s (provedor sobrecarregado?). "
                                                        f"{dica}") from erro
        if isinstance(erro, StopAsyncIteration):
            raise HTTPException(status_code=502, detail=f"o modelo {MODELO} fechou a conexão sem responder. "
                                                        f"{dica}") from erro
        raise HTTPException(status_code=502, detail=f"{type(erro).__name__}: {erro}") from erro
    if primeiro[0] in ("erro", "fim"):
        await contexto.__aexit__(None, None, None)
        motivo = f"falhou: {primeiro[1]}" if primeiro[0] == "erro" else "terminou sem responder"
        raise HTTPException(status_code=502, detail=f"o modelo {MODELO} {motivo}. {dica}")

    async def gerar():
        respondeu = False
        meio_de_linha = False     # o último texto não terminou em \n: nada de linha vazia agora
        ultimo_envio = -SINAL_DE_VIDA
        falhou = False

        def erro_no_fluxo(mensagem: str) -> str:
            print(f"erro no meio da resposta: {mensagem}", file=sys.stderr, flush=True)
            return ("\n" if meio_de_linha else "") + linha_de_erro(f"o modelo {MODELO} {mensagem}. {dica}")

        async def todos():
            yield primeiro
            async for evento in eventos:
                yield evento

        try:
            async for tipo, valor in todos():
                agora = time.monotonic()
                if tipo == "texto":
                    respondeu = True
                    meio_de_linha = not valor.endswith("\n")
                    ultimo_envio = agora
                    yield valor
                elif tipo == "pensando":
                    if not meio_de_linha and agora - ultimo_envio >= SINAL_DE_VIDA:
                        ultimo_envio = agora
                        yield "\n"
                elif tipo == "erro":
                    falhou = True
                    yield erro_no_fluxo(f"falhou: {valor}")
                    break
                else:
                    break
            if not respondeu and not falhou:
                yield erro_no_fluxo("pensou, mas terminou sem responder")
        except asyncio.TimeoutError:
            yield erro_no_fluxo(f"parou de responder por {ESPERA_SEM_ATIVIDADE:.0f} s")
        except Exception as erro:  # noqa: BLE001 - o cabeçalho já foi enviado: o erro vai no fluxo
            yield erro_no_fluxo(f"falhou: {type(erro).__name__}: {erro}")
        finally:
            await contexto.__aexit__(None, None, None)

    return gerar()


@app.get("/saude")
def saude():
    """Confere a configuração sem gastar tokens."""
    return {
        "provedor": PROVEDOR,
        "modelo": MODELO,
        "chave": bool(CHAVE),
        "url": URL or "(biblioteca da Anthropic)",
        "protocolo_bytes": len(sistema),
    }


@app.post("/aula")
async def aula(pedido: Pedido):
    conferir_configuracao()
    if not pedido.mensagens:
        raise HTTPException(status_code=400, detail="nenhuma mensagem enviada")

    mensagens = [
        {"role": "assistant" if m.papel == "professor" else "user", "content": m.texto}
        for m in pedido.mensagens
    ]

    try:
        fluxo = await (abrir_anthropic(mensagens) if PROVEDOR == "anthropic" else abrir_openai(mensagens))
    except HTTPException:
        raise
    except anthropic.APIStatusError as erro:
        # 502: quem falhou foi a API, não o aplicativo (um 401 aqui faria o Qt
        # pedir autenticação do proxy e esconder a mensagem)
        raise HTTPException(
            status_code=502, detail=f"a API respondeu {erro.status_code}: {erro.message}"
        ) from erro
    except Exception as erro:  # noqa: BLE001 - rede fora do ar, modelo inválido etc.
        raise HTTPException(status_code=502, detail=f"{type(erro).__name__}: {erro}") from erro

    # text/plain em pedaços: o aplicativo desenha assim que a primeira linha chega
    return StreamingResponse(fluxo, media_type="text/plain; charset=utf-8")
