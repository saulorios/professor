"""Testes do proxy, sem rede e sem gastar tokens.

    cd proxy && .venv/bin/python -m unittest testes -v
"""

import asyncio
import json
import os
import tempfile
import unittest
from pathlib import Path

# Configuração antes de importar o servidor (ele lê o ambiente na importação)
PASTA_TEMPORARIA = tempfile.mkdtemp(prefix="lousa-testes-")
os.environ.update({
    "LOUSA_PROVEDOR": "openrouter",
    "OPENROUTER_API_KEY": "chave-de-teste",
    "LOUSA_MODELO": "modelo/de-teste:free",
    "LOUSA_CACHE_DIR": PASTA_TEMPORARIA,
    "LOUSA_CACHE": "1",
    "LOUSA_ESPERA_SEM_ATIVIDADE": "2",
})

import httpx  # noqa: E402
from fastapi.testclient import TestClient  # noqa: E402

import servidor as sv  # noqa: E402

sv.SINAL_DE_VIDA = 0.3


def sse(objeto: dict) -> str:
    return "data: " + json.dumps(objeto) + "\n\n"


def texto(t: str) -> str:
    return sse({"choices": [{"delta": {"content": t}}]})


def pensar() -> str:
    return sse({"choices": [{"delta": {"reasoning": "hmm"}}]})


def erro(mensagem: str) -> str:
    return sse({"error": {"code": 502, "message": mensagem}})


FIM = "data: [DONE]\n\n"
AGUARDE = ": OPENROUTER PROCESSING\n\n"


def provedor_falso(partes: list, registro: list | None = None) -> httpx.MockTransport:
    """OpenRouter de mentira: devolve as partes (float = pausa em segundos)."""

    def responder(pedido: httpx.Request) -> httpx.Response:
        if registro is not None:
            registro.append(json.loads(pedido.content))

        async def corpo():
            for parte in partes:
                if isinstance(parte, float):
                    await asyncio.sleep(parte)
                else:
                    yield parte.encode()

        return httpx.Response(200, content=corpo())

    return httpx.MockTransport(responder)


def usar_provedor(partes: list, registro: list | None = None) -> None:
    sv.http = httpx.AsyncClient(transport=provedor_falso(partes, registro))


def perguntar(cliente: TestClient, *textos: str):
    mensagens = [{"papel": "usuario" if i % 2 == 0 else "professor", "texto": t} for i, t in enumerate(textos)]
    return cliente.post("/aula", json={"mensagens": mensagens})


class PromptTest(unittest.TestCase):
    def test_so_o_que_vem_antes_do_marcador_vai_para_a_ia(self):
        with tempfile.NamedTemporaryFile("w", suffix=".md", delete=False, encoding="utf-8") as arquivo:
            arquivo.write("instruções\n<!-- FIM DO PROMPT DA IA -->\ncomandos internos\n")
        prompt = sv.ler_prompt(Path(arquivo.name))
        self.assertIn("instruções", prompt)
        self.assertNotIn("internos", prompt)

    def test_protocolo_real_tem_marcador_e_comandos_novos(self):
        texto_inteiro = sv.PROTOCOLO.read_text(encoding="utf-8")
        self.assertIn(sv.MARCADOR_FIM_DO_PROMPT, texto_inteiro)
        self.assertIn('"tipo":"grafico"', sv.sistema)
        self.assertIn('"tipo":"tabela"', sv.sistema)
        self.assertNotIn("traco_livre", sv.sistema)  # uso interno não vai para a IA

    def test_cache_de_prompt_so_nos_modelos_que_pedem_marcacao(self):
        registro: list = []
        for modelo, espera_lista in (("anthropic/claude-sonnet-5", True), ("modelo/de-teste:free", False)):
            sv.MODELO = modelo
            usar_provedor([texto('{"tipo":"linha"}\n'), FIM], registro)
            with TestClient(sv.app) as cliente:
                perguntar(cliente, "pergunta", "resposta", "outra pergunta")  # sem cache de aulas
            conteudo = registro[-1]["messages"][0]["content"]
            self.assertEqual(isinstance(conteudo, list), espera_lista, modelo)
            if espera_lista:
                self.assertEqual(conteudo[0]["cache_control"], {"type": "ephemeral"})
        sv.MODELO = "modelo/de-teste:free"


class CacheDeAulasTest(unittest.TestCase):
    def setUp(self):
        for arquivo in Path(PASTA_TEMPORARIA).glob("*"):
            arquivo.unlink()

    def test_normaliza_acentos_maiusculas_e_pontuacao(self):
        self.assertEqual(sv.normalizar_pergunta("O que é um  Triângulo?"), "o que e um triangulo")

    def test_primeira_pergunta_e_guardada_e_reaproveitada(self):
        aula = '{"tipo":"fala","texto":"oi"}\n{"tipo":"linha"}\n'
        chamadas: list = []
        usar_provedor([texto(aula), FIM], chamadas)
        with TestClient(sv.app) as cliente:
            primeira = perguntar(cliente, "O que é um triângulo?")
            self.assertEqual(primeira.status_code, 200)
            self.assertEqual(primeira.text, aula)
            self.assertNotIn("x-lousa-cache", primeira.headers)

            segunda = perguntar(cliente, "o que e um triangulo")
            self.assertEqual(segunda.text, aula)
            self.assertEqual(segunda.headers.get("x-lousa-cache"), "reaproveitada")
        self.assertEqual(len(chamadas), 1)  # a IA só foi chamada uma vez

    def test_resposta_com_erro_nao_e_guardada(self):
        usar_provedor([texto('{"tipo":"fala","te'), erro("upstream reset")])
        with TestClient(sv.app) as cliente:
            resposta = perguntar(cliente, "pergunta que falha")
            self.assertIn('"tipo": "erro"', resposta.text)
        self.assertEqual(list(Path(PASTA_TEMPORARIA).glob("*.json")), [])

    def test_perguntas_de_acompanhamento_nao_usam_cache(self):
        self.assertIsNone(sv.chave_do_cache([sv.Mensagem(papel="usuario", texto="a"),
                                             sv.Mensagem(papel="professor", texto="b"),
                                             sv.Mensagem(papel="usuario", texto="c")]))

    def test_trocar_modelo_ou_protocolo_invalida(self):
        mensagens = [sv.Mensagem(papel="usuario", texto="triângulo")]
        original = sv.chave_do_cache(mensagens)
        sv.MODELO, modelo = "outro/modelo", sv.MODELO
        self.assertNotEqual(sv.chave_do_cache(mensagens), original)
        sv.MODELO = modelo
        sv.VERSAO_PROTOCOLO, versao = "outra", sv.VERSAO_PROTOCOLO
        self.assertNotEqual(sv.chave_do_cache(mensagens), original)
        sv.VERSAO_PROTOCOLO = versao


class FluxoTest(unittest.TestCase):
    def setUp(self):
        sv.CACHE_ATIVO = False

    def tearDown(self):
        sv.CACHE_ATIVO = True

    def rodar(self, partes: list):
        usar_provedor(partes)
        with TestClient(sv.app) as cliente:
            return perguntar(cliente, "x")

    def test_erro_antes_de_responder_vira_502(self):
        resposta = self.rodar([AGUARDE, erro("Service temporarily overloaded")])
        self.assertEqual(resposta.status_code, 502)
        self.assertIn("overloaded", resposta.json()["detail"])

    def test_so_aguarde_vira_504(self):
        resposta = self.rodar([AGUARDE, 1.0, AGUARDE, 1.0, AGUARDE, 1.0, AGUARDE])
        self.assertEqual(resposta.status_code, 504)

    def test_pensando_manda_sinal_de_vida_e_depois_a_aula(self):
        resposta = self.rodar([pensar(), 0.35, pensar(), 0.35, pensar(), texto('{"tipo":"linha"}\n'), FIM])
        self.assertEqual(resposta.status_code, 200)
        self.assertTrue(resposta.text.startswith("\n"))
        self.assertTrue(resposta.text.endswith('{"tipo":"linha"}\n'))

    def test_erro_no_meio_vira_linha_de_erro_inteira(self):
        resposta = self.rodar([texto('{"tipo":"fala","te'), erro("upstream reset")])
        ultima = [linha for linha in resposta.text.splitlines() if linha.strip()][-1]
        self.assertEqual(json.loads(ultima)["tipo"], "erro")

    def test_uso_de_tokens_nao_vai_para_o_app(self):
        uso = sse({"choices": [], "usage": {"prompt_tokens": 10, "completion_tokens": 5,
                                            "prompt_tokens_details": {"cached_tokens": 8}}})
        resposta = self.rodar([texto('{"tipo":"linha"}\n'), uso, FIM])
        self.assertEqual(resposta.text, '{"tipo":"linha"}\n')


if __name__ == "__main__":
    unittest.main()
