# FASES — Projeto Amanda (controle de implementação)

- [x] Fase 1 — Núcleo Windows em C puro (`amandac.exe`): extractor PDF/TXT/CSV/JSON, chunker, embedder 384d, question_gen (choice/score/noul), decision engine híbrido, packager `.amanda` com CRC, server HTTP OpenAI-compatible, CLI compile/serve/ask/inspect/version, `build.bat`, `version.bin` 1.0.1, testes unitários + integração.
- [x] Fase 2 — CMake multiplataforma + CI: `CMakeLists.txt` validado localmente (Ninja+GCC, `ctest` OK), workflow `.github/workflows/ci.yml` (Windows/Linux/Mac: build + unit + integração + serve smoke + Laya SKIP), código endurecido p/ POSIX (sem warnings novos).
- [x] Fase 3 — Integração Laya via HTTP, sem GGUF: `amandac serve` como backend OpenAI-compatible do Laya (LiteLLM) + backend opcional `laya-http` no decision_engine com fallback local. `amandac ask/eval --backend laya-http --laya-url URL`; etapa `scripts\check_laya_llm.bat` (LIVE ou SKIP honesto). Verificado com Ollama `nimble:latest` cadastrado no Laya; caminho vivo requer slot chat apontado p/ nimble. Ver `docs/laya.md`.
- [x] Fase 4 — Servidor SSE + `/v1/embeddings`: `stream:true` retorna `text/event-stream` com deltas + `data: [DONE]`; `POST /v1/embeddings` (string ou array, 384 floats, formato OpenAI); fixtures `examples/smoke_*.json`; smoke no CI e na pipeline `.bat/.sh`.
- [x] Fase 5 — Calibração com validação em 10% das perguntas + métricas (fidelidade/calibração/latência/recusa).
- [x] Fase 6 — Recalibração: `amandac calibrate` (grade centro/inclinação/limiar sobre amostra + probes, maximiza acurácia balanceada), motor com sigmoide parametrizável (`--conf-center/--conf-slope/--limiar-recusa` em `ask`/`eval`, zeros = padrão histórico), etapa na pipeline + CI.
- [x] Fase 7 — Endurecimento (microfases 7.1–7.6, todas entregues):
  - [x] 7.1 Docs e higiene: `Projeto.md`/`Jimi.md`/`models/README.md` sincronizados (locais), warning `has_choice` eliminado, `version_bump` no CMake (paridade com `build.bat`).
  - [x] 7.2 Serve calibrado: flags `--conf-center/--conf-slope/--limiar-recusa` no `serve` (backend segue local; LLM-por-request volta na 7.5 com threads).
  - [x] 7.3 Config + templates vivos: `compile --config` (YAML subset, CLI > config), `question_gen` renderizando `templates/*.tpl` com fallback embutido (byte-identico), `--templates-dir`, `--max-choice/score/noul`.
  - [x] 7.4 Extração PDF+: operadores Tj/TJ/`'`/`"` + quebras Td/TD/Tm/T*, filtros
    ASCIIHex/ASCII85/RunLength + cadeia multi-filtro, dicionário próprio por stream
    (fim da contaminação entre objetos), correção do inflate dinâmico
    (`bl_count[0]=0`, RFC 1951 — falhava em ~95% dos streams reais), concatenação
    TJ com kerning, WinAnsi→UTF-8, tolerância com stats, formato `.amanda` v2
    (cobertura persistida, leitor aceita v1), cobertura no `eval` (texto + `--json`).
    Testes 83/83. Nos 4 livros: 0 falhas (CVM 448 blocos/412p fid 86%,
    IBRI 178/161p fid 90%, Invest 253/260p fid 90%, Direito 1393/1348p).
  - [x] 7.5 Servidor robusto: thread por conexão (cap `--max-conns`, cheio = 503),
    CORS configurável (`--cors`, preflight completo), auth Bearer opcional
    (`--api-key`, sem chave = aberto, nunca loga a chave), limites
    (`--max-body` = 413, header 64KB = 431, recv timeout 30s),
    `POST /v1/eval` (sample/seed/top_k, teto `--eval-max` = 400 honesto).
    Testes 98/98 (suite `serve_75` com sockets reais: 401/413/CORS/eval/concorrência).
  - [x] 7.6 Release: artefatos CI (`amandac` + `amanda_tests`, win+linux) +
    `inspect --json` (relatório máquina com formato/páginas/perguntas/cobertura).
    Testes 101/101, pipeline com smoke do `--json` e do `POST /v1/eval`.
- [x] Fase 10 — Robustez extração/eval + guia de uso: limiares TJ configuráveis
  (`compile --tj-espaco/--tj-salto`, seção `extracao:` no YAML, defaults
  -100/+500), `eval --max-amostras` (teto determinístico p/ bases gigantes)
  + progresso em `stderr`,   guia `docs/guia_uso.md` (fluxo completo, 1 `.amanda`
  por livro, serve, troubleshooting), fixtures `smoke_chat/decisions.json`.
  Testes 108/108. macOS segue fora do CI (falha ARM pré-existente desde 7.2;
  reativar com diagnóstico em Mac real — sem fingir correção).
- [x] Fase 11 — Serve com LLM vivo: `serve --backend laya-http`
  (`--laya-url`, `--laya-timeout-ms`, `--laya-max` com fallback local
  imediato sem slot), grounding sempre local, campo `"backend"` nas
  respostas, smoke `serve` no `check_laya_llm` (SKIP sem engine).
  Testes 117/117 (stub Laya: vivo, fallback, teto de concorrência).
  Fix pós-entrega (2026-10-01): `signal(SIGPIPE, SIG_IGN)` em
  `src/main.c` + `tests/test_all.c` (só POSIX) — stub+serve em threads
  morria com SIGPIPE no Linux (CI ubuntu exit 8, sem artefato Linux),
  no Windows passava. Sem o ignore, `send()` em socket fechado mata o
  processo (inclusive o `serve` em produção); com ele, retorna EPIPE e
  os erros já tratados cuidam. Cuidado futuro: todo código com sockets
  precisa passar no Linux (WSL serve p/ reproduzir) — verde só no
  Windows não prova nada p/ POSIX. Verificado 117/117 no WSL Ubuntu.
- [x] Pós-11 (2026-10-01, entregas sem nova fase numerada):
  - Gold sets por livro: `examples/gold_{cvm,ibri,invest,direito}.json`
    (10 perguntas + `pagina_esperada` cada, curadas contra os
    `*_t74.amanda`) + `scripts/check_gold.bat` (40/40 PASS, FAIL com
    exit 1, SKIP sem artefatos). Cuidado: `pagina_esperada` é
    comportamento observado estável, não verdade auditada — revisar
    por amostragem antes de tratar como ouro absoluto.
  - Packs `.tpl` empresariais: `templates/empresas/{compliance,
    financeiro,juridico,atendimento}/` (mesmos 3 arquivos fixos e
    variáveis do motor, só wording) + `docs/tutorial_empresas.md`
    (criar tpl, compilar, comandos, modelos, servidor local/VPS).
    Cuidado: trocar wording muda scores — sempre `calibrate` depois;
    nunca editar `templates/question_*.tpl` base.
  - `README.md` com descrição empresarial do projeto.
- [ ] Fase 12 — Retrieval + qualidade (Fase 12.2 entregue 2026-10-02,
  `amandac 1.0.26`, testes 125/125, gold v2 40/40):
  - [x] 12.2 Léxico BM25 + stopwords PT + multi-citação:
    `tokenizar()` dobra UTF-8/Latin1 p/ ASCII + ~130 stopwords PT
    (vale p/ embeddings e retrieval juntos); `recuperar_chunks()`
    troca overlap por BM25 (k1=1.2, b=0.75, IDF por pacote, max-norm)
    mantendo fusão 0.6*cos+0.4*bm25 (pesos/CLI/API/formatos intactos);
    `executar_decisao()` cita top-2 (`[p.X]` + `[p.Y]`, 400 chars cada,
    header `p. X, Y`) sem mudar struct nem `.amanda`.
    Eval estável (CVM 85.1%/-1.2pp, IBRI 88.6%/-1.1pp, INVEST 90.0%/+0.2pp,
    tudo dentro do ruído ±1.5pp; latência 30–70ms PASS); probes exemplo
    0/3→2/3. Gold v2: 20 pins atualizados (auditados, vários melhores —
    ação ordinária→cap. Espécies, análise técnica→capítulo, segregação→
    frase exata), 4 mantidos via 2ª citação, `check_gold` recall@2
    (pagina OU `[p.N]`). Limitações conhecidas auditadas: RI→top1
    planejamento (RI correto em 2º), títulos sustentáveis→top1 p.61 com
    sujeira binária do PDF (correto em p.198/209) — ver backlog 12.3.
  - [x] 12.3 Rerank robusto (2026-10-02, `amandac 1.0.31`, 131/131, gold
    80/80): stemming PT conservador em `tokenizar()` (oes→ao, ais→al,
    plural/verbo/gerúndio/particípio c/ travas; sem gênero); filtro junk
    (token >30 chars ou char 5x, chunk inválido c/ <4 termos ou <40%
    letras); BM25 c/ norma saturante raw/(raw+8) (max-norm colapsava
    c/ outlier e entregava o rank ao cosseno ruidoso); phrase-boost +0.2
    (janela de tokens, sem alloc por chunk — 3811 mallocs/query
    estouravam a latência do Direito 447→503ms, revertido p/ 447ms).
    Sem formato v3 (TF-IDF em embeddings adiado: BM25 já é o canal IDF).
    Ganhos: títulos→top1 189 correto (era junk 61), RI→cap. RI 189
    (era 374), polícia→frase exata 896, valores→definição legal 66.
    Custo honesto: fidelidade sintética -2pp (CVM 88.65→86.58, IBRI
    89.19→87.25, INVEST -0.4pp ruído; Direito ~84.5%) — queries naturais
    melhoram, guardrail segue verde. 15 repins + 3 swaps auditados
    (pergunta instável em 3 runs → reformulada p/ respondível).
  - [x] 12.4 Performance/índice (entregue abaixo; era backlog: índice
    invertido, cache de query, calibrate --apply).
- [x] Fase 12.4 — Performance/índice + calibrate --apply (2026-10-02,
  testes 150/150, gold 80/80 em v2 sem recompilar):
  - Índice BM25 pré-tokenizado 1x por pacote (`RetrievalIndex` em
    `decision_engine.c`: tokens + postings + df por pacote, mesma
    matemática 12.2/12.3 — equivalência provada em teste); `eval` e
    `calibra` constroem 1x por run; `serve` compartilha 1x por pacote
    (somente leitura, seguro p/ threads); `ask` 1x por invocação.
    Direito 447ms → ~6ms (amostra 1%, 332q, mesma fidelidade 82.8%).
  - Cache de embeddings de query (global, FIFO 32, thread-safe com
    mutex; chave = string exata; hit devolve cópia).
  - `calibrate --apply [--output]` grava sugerido no pacote, formato
    `.amanda` **v3** (`tem_calib` + center/slope/limiar; leitor aceita
    v1/v2/v3; ferramenta em evolução — pacotes antigos recompilados,
    sem migração de produção). Precedência: flag CLI > pacote >
    padrão histórico; `--ignore-calib` em ask/eval/serve. `inspect`
    texto+json expõe `calibracao`. Exemplo real: `exemplo.amanda`
    probes 2/3→3/3, gap 0.054→0.000.
  - Bug achado no caminho: leitor aceitava só v1+v3 (`fmt != 1 &&
    fmt != VERSAO`) e rejeitava os 4 livros v2 (gold 0/80) — corrigido
    p/ aceitar 1/2/3 + teste de formato atualizado p/ 3.
- [x] Fase 12.4b — calibração com validação natural (2026-10-02,
  testes 161/161, 4 livros em v3 aplicada):
  - Causa raiz do tradeoff documentada com dados: probes partilham
    vocabulário ("capital de Marte" conf 0.90!) — sobreposição real,
    inseparável por limiar; recusar todo OOD implica recusar a cauda
    natural. Para os verticais (respostas extrativas+citadas), a
    recusa sob sobreposição é a ação calibrada correta.
  - Soma BM25 em `double` (bit-idêntica ao legado), grade fina
    (limiar 0.01, 13608 combos), `calibrate --validacao <gold.json>`
    (repetível até 8): naturais fora do `bal`, no termo `recall@2`
    do objetivo `(bal + recall@2)/2` (sem validacao = Fase 6 intacta).
  - Pontos aplicados: CVM 0.2/16/0.85 (vr 19/20), IBRI 0.2/22/0.89
    (18/20), INVEST 0.25/30/0.82 (16/20), Direito 0.25/30/0.89
    (19/20). Eval: 86.64/87.41/88.27/84.00%, lat 0.3–5ms, probes 3/3.
  - Gold 75/80: 4 recusas com rank correto (RI, CVM-função, títulos,
    TIR — respondiam em limiar 0.30) + 1 near-tie (preferencial
    1071→1070, tópico difuso em 122 chunks sem âncora). Pins
    mantidos (sem repin p/ caber no motor); `--limiar-recusa`
    sobrescreve por deploy.
- [x] Fase 13 — Serve enterprise (2026-10-02, testes 181/181):
  - Pool fixo de workers (`--workers`, default 8, teto 64) + fila
    limitada; `max_conns` segue o teto total (ativas+fila, 503).
  - `--api-key` via flag > env `AMANDA_API_KEY` > `--api-key-file`
    (trim); sem chave = aberto; nunca logada.
  - `serve --config` (seção `servidor:`: porta/host/pacote/cors/
    chave/workers/limites/backend/laya; flag CLI prevalece).
  - Log de acesso em `stderr` (hora, método, rota, código, ms —
    sem corpo nem chave). Sem TLS próprio (reverse-proxy p/ HTTPS).
  - Multi-pacote: `--package` repetível (`nome=caminho`, até 8);
    `"model"` seleciona (`"amanda"`/omitido = 1º); `/v1/models`
    lista; `decisions`/`eval` aceitam `"model"`; desconhecido = 404
    com lista; cada pacote com calib v3 + índice próprios.
- [x] Pós-12.2 (2026-10-02, base completa + testes, sem código C):
  higiene `build/` (removidos 4 `.amanda` legados pré-7.4 superseded),
  `calibrate` por livro registrado em `docs/eval.md` (CVM 0.450/24/0.80,
  IBRI 0.450/16/0.85, INVEST 0.450/28/0.75, Direito 0.550/14/0.40),
  eval full (CVM 88.65%, IBRI 89.19%, INVEST 88.63%, Direito 85.95%
  em 1993; lat Direito 447ms PASS perto do teto), gold v3 20/livro
  (80/80 recall@2; 40 novas auditadas, 5 descartadas por fora do
  domínio — usucapião/licitação/duration/guidance/silêncio).
- [ ] Fase 14 — CI macOS (PAUSADA 2026-10-02, revertida p/ win+linux):
  reativado como `macos-15` (M1) + `macos-15-intel`, build OK nas 2
  archs mas unit tests morrem em 0s em ambas (run 37075569182) —
  crash na partida, igual em ARM e Intel (não é bug ARM). Auditoria
  prévia sem achados (sem intrínsecos x86, LE byte-a-byte, SIGPIPE
  portátil, stack ~70KB < 512KB, Clang limpo). Reativar com o log da
  etapa Unit tests ou teste local num Mac. M2 só em larger pagos,
  M3/M4 sem labels — essas máquinas via self-hosted (roteiro futuro).

# Futuro (pós-13: Docker + MCP entregues 2026-10-05, Pool LLM
2026-10-05, Backends reais 2026-10-06 — `amandac 1.0.51`, testes
267/267; ver seções abaixo; restante planejado)
- [x] Docker: imagem com `amandac` + `serve` como entrypoint (multi-pacote por volume).
- [x] MCP server: expor `ask`/`decisions` como ferramentas MCP p/ OpenCode e agentes.
- [x] Pool LLM: fila própria com prioridade p/ inferências `laya-http` (ver "Fase Pool LLM" abaixo).
- [ ] Testes em GPU: Laya vivo (nimble + llama3.2:3B) em GTX 1660 Ti e GPU 10GB+ (ver `docs/laya.md`).
- [ ] Self-hosted M2-M4: documentar runner próprio (labels + serviço) rodando as etapas do CI.
- [ ] CI macOS: ver Fase 14 (pausada com diagnóstico registrado) e `docs/macos.md` (kit futuro).
- [ ] Auditoria planejado × implementado: `Projeto.md` previa MuPDF/ONNX/GGUF local —
  implementado diverge de propósito (parser PDF próprio, TF 384d local, Laya via
  HTTP; ver `Projeto.md` § estado + `docs/laya.md`). Sem lacuna funcional aberta.

- [x] Fase MCP/Docker (2026-10-05, escopo confirmado Skill nº5: Docker + MCP):
  - `amandac mcp` (`include/mcp.h`, `src/mcp.c`): MCP stdio JSON-RPC
    (linha + framing LSP `Content-Length`), métodos `initialize`/`ping`/
    `tools/list`/`tools/call`, `notifications/*` sem resposta; erros
    `-32700/-32601/-32602` com `id` verbatim; ferramentas `ask`
    (texto + confiança/página/citação), `decisions` (JSON cru),
    `inspect`, `version`; multi-pacote por `model` (mesma regra do
    serve); calibração v3 por pacote (`usar_calib_pkg`), flags CLI
    prevalecem, `--ignore-calib` como no `ask`; stdout só JSON-RPC.
  - `Dockerfile` multi-stage (build roda `amanda_tests`; runtime
    debian-slim, usuário sem root) + `scripts/docker-entrypoint.sh`
    (serve todos `/data/*.amanda`, env `PORT/HOST/WORKERS/chave/
    calibração/backend`; argv ≠ serve executa direto) +
    `.dockerignore` + `docs/docker.md` + `docs/mcp.md` +
    `examples/mcp_config.json` (modelo).
  - Testes 198/198 (+17 `test_mcp`: protocolo, ferramentas, erros,
    ids, multi); `scripts/check_mcp.bat/.sh` + `check_docker.bat/.sh`
    (SKIP sem docker); pipeline 10 etapas; CI com MCP smoke (win+
    linux) e `docker build` no ubuntu (este último removido do CI
    em 2026-10-06 — sem docker p/ validar; segue local via
    `check_docker`).
  - Bugs achados no caminho: `-Wcomment` por `/*` em comentário de
    `mcp.h` (zero warnings novos, como exige o projeto);
    `strncmp(method, "notifications/", 16)` com tamanho errado
    (notificação respondia — pego pelo teste, corrigido p/ 14).

- [x] Fase Pool LLM (2026-10-05, escopo confirmado Skill nº5 junto com
  `check_gold.sh`; `amandac 1.0.39`, testes 220/220):
  - `LlmPool` (`include/laya_backend.h`, `src/laya_backend.c`, C11
    portátil: `CRITICAL_SECTION`+`CONDITION_VARIABLE` no Windows,
    `pthread`+`cond_timedwait` no POSIX): fila própria com 2
    prioridades FIFO (ALTA p/ `decisions`, NORMAL p/ `chat`),
    transferência direta de slot ao escolhido (sem roubo por
    recém-chegado), timeout com deadline (`now_ms`), corrida
    grant-vs-timeout tratada (granted sob o mutex vence), contadores
    `atendidas/fb_fila/fb_tempo`, `max_fila=0` = legado imediato.
  - `serve`: `--laya-queue N` (default 16, `0` = sem espera) +
    `--laya-queue-ms MS` (default 5000, `0` = tenta uma vez),
    `serve --config` (`servidor: laya_queue/laya_queue_ms`), banner
    com fila/espera; `decisions` ALTA, `chat` NORMAL; cheia/estouro =
    fallback local honesto (campo `backend` intacto).
  - `serve`: `--laya-queue N` (default 16, `0` = sem espera) +
    `--laya-queue-ms MS` (default 5000, `0` = tenta uma vez),
    `serve --config` (`servidor: laya_queue/laya_queue_ms`), banner
    com fila/espera; `decisions` ALTA, `chat` NORMAL; cheia/estouro =
    fallback local honesto (campo `backend` intacto).
  - Docker (`LAYA_QUEUE`/`LAYA_QUEUE_MS`), `docs/laya.md`,
    `docs/api.md`, `README.md`, `examples/config.yaml` (chaves
    comentadas). `serve`/`ask`/`mcp` locais inalterados.
  - Testes +22 `test_llm_pool` (defaults, espera 0, timeout com
    prazo, fila cheia, waiter recebe, ALTA> NORMAL, FIFO, config
    YAML); `scripts/check_gold.sh` (port fiel do `.bat`, 80/80 pins
    idênticos verificados por script, SKIP sem `*_t74`, exit 1 em
    divergência) + etapa CI (`|| true`: CI não tem os livros).

- [x] Fase Backends reais (2026-10-06, escopo Skill nº5 "tudo":
  TypeSafe/JEV + DeepSeek + amanda.json + exemplos + pipeline real;
  `amandac 1.0.50`, testes 267/267
  (alias `jev` no amanda.json p/ o caminho do JEV):
  - Transporte `http_post_json` (`laya_backend`): `http://` pelo
    socket nativo, `https://` via curl do sistema (TLS real, Bearer
    por arquivo de headers — nunca na linha de comando); Bearer em
    `http://` recusado (chave nunca em claro).
  - `typesafe-http` (alias `jev`, `typesafe_backend.c`): System One
    real — nuvem `api.typesafe.ai` (`jev-latest`, Bearer
    `TYPESAFE_API_KEY`) ou nimble no Ollama (sem chave); `noul`
    calibra prob/conf sobre grounding local; URL base ou completa.
    Verificado vivo: nimble `noul=0.99` + `ask` ponta a ponta
    (`backend: typesafe-http`, citacao local).
  - `deepseek-http` (`deepseek_backend.c`): chat OpenAI-compatible
    (`deepseek-chat`, Bearer `DEEPSEEK_API_KEY`) redige sobre a
    citacao; `NAO CONSTA` preservado.
  - Motor: `hibrida_remota` única (`decision_engine.c`), `via` 0–3 +
    `decision_backend_nome()` em ask/serve/MCP/eval; `eval` conta
    `via_typesafe/via_deepseek` (JSON + texto); pool LLM
    compartilhado no serve (decisions ALTA).
  - Flags `--typesafe-/--deepseek-url|model|timeout|key|key-file`
    em ask/eval/serve/mcp (+ `serve --config` YAML e Docker env);
    `amanda.json` (`--config-json`, overlay sem reset, sem segredos;
    template em `examples/`; `/amanda.json` no `.gitignore`).
    Precedencia: flags > json > yaml.
  - Bugs achados: extrator noul casava `"type":"noul"` antes do valor
    (exige `:` apos a chave — pego em sonda contra nimble real);
    stub de teste lia corpo apos o cliente travar (break antes do
    recv — deadlock); stub sem `WSAStartup` apos Cleanup dos testes
    de serve; `ask` posicional engolia `--typesafe/deepseek-*`
    (skip list). Todos com teste.
  - Segunda passagem (nuvem real, `TYPESAFE_API_KEY` do usuário via
    `amandac.conf`): `check_typesafe` 100% — nimble OK, `jev-latest`
    OK, 4/4 pins dos livros (cvm 139, ibri 70, inv 81, dir 531).
    Bugs reais achados: (1) Bearer em http era recusado sempre —
    quebrava o nimble com chave no conf; agora vale em loopback
    (não sai da máquina), https fora disso; (2) `\n` literal no `-w`
    do curl quebrava o `cmd /c` (https nunca funcionou no Windows);
    marcador sem newline + `2>&1` p/ diagnóstico. `AMANDA_DEBUG=1`
    expõe o motivo do fallback (sem chaves). Total 267/267.
  - Calibração dos livros: NÃO mexida (decisão documentada) — v3 é
    do motor local; no JEV só o limiar atua; evidência fina (5
    julgamentos); alavanca por deploy (`--limiar-recusa`) já resolve.
  - Base de testes p/ limiar do JEV: `scripts/sweep_typesafe.bat/.sh`
    (curva limiar × nuvem, 4 pins × 5 limiares = 20 chamadas rápidas,
    SKIP sem chave/livros, exit 0 — nunca falha por tradeoff).
    Sem código C; evidência futura antes de qualquer default por
    backend. Guia do teste real só-nuvem em `docs/guia_jev.md` §4b.
  - Testes +32 (stubs SystemOne/DeepSeek: judge/redact/401/timeout/
    hibrida via=2/fallback/secret/env-file/json/endpoint) +
    `check_typesafe.bat/.sh` (nimble + nuvem + 1 pin/livro, SKIP por
    etapa) + `check_deepseek.bat/.sh` (SKIP sem chave); pipeline 12
    etapas; CI com as etapas (`|| true`: sem Ollama/chaves/livros).
  - Docs: `docs/typesafe.md`, `docs/deepseek.md`, `docs/guia_jev.md`
    (guia hands-on: testar, analisar, criar exemplos + teste real
    só-nuvem 5/5),
    `examples/amanda.json` + `typesafe_request.json`, READMEs PT/EN/
    RU/ZH, `docs/docker.md`, guia.
  - Pós-entrega (pedido do usuário): `amandac.conf` (`KEY=valor` na
    raiz, gitignored, template em `examples/`) como fonte de chaves:
    precedência flag > env > `--key-file` (KEY=valor ou raw) >
    conf (`AMANDA_CONF` troca o caminho); `check_typesafe/deepseek`
    (`.bat`+`.sh`) detectam a chave no conf sem exibi-la; +11 testes
    (parser, cadeia, overlay). Total 263/263.
