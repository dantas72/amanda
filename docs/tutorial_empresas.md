# Tutorial Amanda para empresas — packs `.tpl`, compilação e servidor

Guia end-to-end: criar templates por segmento, extrair e compilar
documentos, usar com modelos e servir local ou numa VPS.
Pré-requisito: `build.bat` já rodado (`amandac.exe` na raiz).
Detalhes de referência em `docs/guia_uso.md`, `docs/api.md`,
`docs/laya.md` e `docs/eval.md`.

## 1. Packs empresariais prontos (`templates/empresas/`)

| pack | uso |
|---|---|
| `compliance/` | normas, conformidade, auditoria, itens de controle |
| `financeiro/` | relatórios, indicadores, mercado, RI |
| `juridico/` | contratos, teses, prova, responsabilidade |
| `atendimento/` | base de conhecimento e FAQ de suporte |

Uso:

```bat
amandac.exe compile --input norma.pdf --output build\norma.amanda --templates-dir templates\empresas\compliance
```

Validados em `amandac 1.0.19` (banner `compile: templates de '...'`,
16/16 perguntas no exemplo, `eval` 100% fidelidade).

## 2. Como criar um `.tpl` (formato exato)

O motor aceita **exatamente 3 arquivos** (nomes fixos) por diretório:

- `question_choice.tpl` — variáveis: `{{trecho}}` `{{pagina}}`
- `question_score.tpl` — variáveis: `{{trecho}}` `{{min}}` `{{max}}` `{{pagina}}`
- `question_noul.tpl` — variáveis: `{{afirmacao}}` `{{pagina}}`

Formato do arquivo: JSON com campo `"enunciado"` (recomendado, como
os packs prontos) ou texto puro (o arquivo inteiro vira o formato).
Chaves `{{...}}` desconhecidas são mantidas literais — não invente
variáveis novas, o motor não as preenche.

Regras obrigatórias:

1. **Sem acentos** (ASCII puro — o extrator e o `eval` operam sem eles).
2. **Enunciado ≤ 2048 caracteres**.
3. **Manter os nomes de arquivo** — outro nome é ignorado.
4. Arquivo ausente/ilegível = fallback embutido (a compilação nunca
   falha por template, mas avisa em `stderr`).
5. **Nunca edite `templates/question_*.tpl` base** — eles são
   byte-idênticos ao fallback do binário. Crie um diretório novo.

Passo a passo de um pack novo (ex. `logistica/`):

```bat
mkdir templates\empresas\logistica
```

Copie um pack pronto como base, ajuste só o `enunciado` e compile:

```bat
amandac.exe compile --input manual.pdf --output build\manual.amanda --templates-dir templates\empresas\logistica
```

**Depois de mudar o wording, rode `calibrate`** — o texto do
enunciado altera os scores, e os limiares antigos podem não servir:

```bat
amandac.exe calibrate --package build\manual.amanda --sample 0.2
amandac.exe eval --package build\manual.amanda --sample 0.1 --conf-center 0.45 --conf-slope 26 --limiar-recusa 0.70
```

Adote os parâmetros se `bal` subir e as probes fecharem 3/3.
Também dá para fixar o pack no YAML (`templates: dir: ...`) —
precedência: flag CLI > config > padrão.

## 3. Extrair e compilar documentos

Entradas: `.pdf` `.txt` `.csv` `.json`. Um `.amanda` **por fonte**
(nunca fundir livros — derruba a fidelidade):

```bat
amandac.exe compile --input "pdf\manual.pdf" --output build\manual.amanda --title "Manual" --templates-dir templates\empresas\atendimento
amandac.exe inspect --package build\manual.amanda --stats
```

O `compile` imprime a cobertura (`streams`, `texto`, `falhas`,
`fallback`). `falhas` deve ser 0; `fallback=sim` = PDF problemático
(escaneado/imagem — passe OCR antes). PDFs com palavras grudadas ou
separadas: ajuste `--tj-espaco -100 --tj-salto 500` (ou seção
`extracao:` no YAML) e recalibre depois.

## 4. Comandos (tabela rápida)

| comando | para que |
|---|---|
| `compile` | PDF/TXT/CSV/JSON → `.amanda` |
| `inspect --stats` / `--json` | conferir chunks, perguntas, cobertura |
| `ask --package X "pergunta"` | resposta grounded com página e confiança |
| `eval --package X --sample 0.1 [--json]` | fidelidade/calibração/latência/recusa |
| `calibrate --package X --sample 0.2` | sugere `--conf-center/--conf-slope/--limiar-recusa` |
| `serve --package X --port 8080` | API OpenAI-compatible (ver § 6) |
| `version` | versão do binário (`version.bin`) |

Validação de regressão por livro: `examples/gold_{cvm,ibri,invest,direito}.json`
(10 perguntas cada com `pagina_esperada`). Automatizado:

```bat
scripts\check_gold.bat
```

40/40 PASS = exit 0; qualquer divergência = FAIL com exit 1; sem os
pacotes `*_t74.amanda` em `build\` = SKIP honesto. Divergência indica
regressão do pacote ou do pack de templates.

## 5. Usar com modelos (Laya / Ollama)

Padrão o motor é **local** (recuperação híbrida + recusa calibrada).
Com o engine do Laya no ar (`:8420` com slot `chat` apontado p/ um
modelo com provider — ex. `ollama/llama3.2:3B`), o Laya redige sobre
o grounding local:

```bat
amandac.exe ask --package build\manual.amanda "Como abro um chamado?" --backend laya-http
amandac.exe serve --package build\manual.amanda --port 8080 --backend laya-http --laya-max 2
```

A resposta traz `"backend":"laya-http"` (ou `"local"` no fallback
honesto) e `(via Laya)` no texto quando o LLM respondeu.
`embeddings` e `eval` seguem sempre locais. Detalhes em `docs/laya.md`.

## 6. Servidor local

```bat
amandac.exe serve --package build\manual.amanda --port 8080 --api-key TROQUE_ESTA_CHAVE
```

Flags principais: `--conf-center/--conf-slope/--limiar-recusa`
(calibração), `--cors`, `--api-key` (sem chave = aberto),
`--max-body`, `--max-conns`, `--eval-max`. Rotas em `docs/api.md`:
`GET /v1/models`, `GET /v1/amanda/info`,
`POST /v1/chat/completions` (com `"stream": true` p/ SSE),
`POST /v1/decisions`, `POST /v1/embeddings`, `POST /v1/eval`.

```bat
curl.exe -s http://127.0.0.1:8080/v1/models
curl.exe -s -X POST http://127.0.0.1:8080/v1/chat/completions -H "Content-Type: application/json" -d "@examples\smoke_chat.json"
```

## 7. Servidor numa VPS (Linux)

Baseado no fluxo Linux do repositório (`CMakeLists.txt`,
`scripts/test_pipeline.sh`) — não testado nesta máquina Windows:

```sh
# dependências
sudo apt update && sudo apt install -y gcc cmake ninja-build curl
git clone https://github.com/dantas72/amanda.git && cd amanda
cmake -S . -B build -G Ninja && cmake --build build
./build/amandac version
./build/amandac compile --input examples/exemplo.txt --output /srv/amanda/manual.amanda
```

Serviço `systemd` (`/etc/systemd/system/amandac.service`):

```ini
[Unit]
Description=Amanda knowledge server
After=network.target

[Service]
ExecStart=/srv/amanda/build/amandac serve --package /srv/amanda/manual.amanda --port 8080 --api-key TROQUE_ESTA_CHAVE
Restart=always
User=amanda

[Install]
WantedBy=multi-user.target
```

```sh
sudo systemctl enable --now amandac
sudo ufw allow 8080/tcp   # ou exponha só via proxy reverso
```

Recomendações: nunca exponha sem `--api-key`; prefira um proxy
reverso (nginx/Caddy) com TLS terminando o HTTPS e o `amandac`
ouvindo só em `127.0.0.1`; um `.amanda` por serviço/livro;
para LLM na VPS, instale Ollama + Laya na mesma máquina e use
`--backend laya-http` (com `--laya-max` baixo em CPU pequena).

## 8. Roteiro empresa completo (exemplo)

```bat
amandac.exe compile --input "pdf\norma.pdf" --output build\norma.amanda --templates-dir templates\empresas\compliance
amandac.exe inspect --package build\norma.amanda --stats
amandac.exe eval --package build\norma.amanda --sample 0.1
amandac.exe calibrate --package build\norma.amanda --sample 0.2
amandac.exe serve --package build\norma.amanda --port 8080 --api-key TROQUE_ESTA_CHAVE
```
