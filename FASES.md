# FASES — Projeto Amanda (controle de implementação)

- [x] Fase 1 — Núcleo Windows em C puro (`amandac.exe`): extractor PDF/TXT/CSV/JSON, chunker, embedder 384d, question_gen (choice/score/noul), decision engine híbrido, packager `.amanda` com CRC, server HTTP OpenAI-compatible, CLI compile/serve/ask/inspect/version, `build.bat`, `version.bin` 1.0.1, testes unitários + integração.
- [x] Fase 2 — CMake multiplataforma + CI: `CMakeLists.txt` validado localmente (Ninja+GCC, `ctest` OK), workflow `.github/workflows/ci.yml` (Windows/Linux/Mac: build + unit + integração + serve smoke + Laya SKIP), código endurecido p/ POSIX (sem warnings novos).
- [x] Fase 3 — Integração Laya via HTTP, sem GGUF: `amandac serve` como backend OpenAI-compatible do Laya (LiteLLM) + backend opcional `laya-http` no decision_engine com fallback local. `amandac ask/eval --backend laya-http --laya-url URL`; etapa `scripts\check_laya_llm.bat` (LIVE ou SKIP honesto). Verificado com Ollama `nimble:latest` cadastrado no Laya; caminho vivo requer slot chat apontado p/ nimble. Ver `docs/laya.md`.
- [x] Fase 4 — Servidor SSE + `/v1/embeddings`: `stream:true` retorna `text/event-stream` com deltas + `data: [DONE]`; `POST /v1/embeddings` (string ou array, 384 floats, formato OpenAI); fixtures `examples/smoke_*.json`; smoke no CI e na pipeline `.bat/.sh`.
- [x] Fase 5 — Calibração com validação em 10% das perguntas + métricas (fidelidade/calibração/latência/recusa).
- [x] Fase 6 — Recalibração: `amandac calibrate` (grade centro/inclinação/limiar sobre amostra + probes, maximiza acurácia balanceada), motor com sigmoide parametrizável (`--conf-center/--conf-slope/--limiar-recusa` em `ask`/`eval`, zeros = padrão histórico), etapa na pipeline + CI.
- [ ] Fase 7 — Endurecimento e pendências (microfases independentes):
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
  - [ ] 12.3 Rerank robusto (backlog): stemming PT, TF-IDF nos embeddings
    (hoje TF puro c/ colisões favorece chunk curto/sujeira no cosseno),
    filtro de chunks não-linguísticos, boost de proximidade de frase.
- [x] Pós-12.2 (2026-10-02, base completa + testes, sem código C):
  higiene `build/` (removidos 4 `.amanda` legados pré-7.4 superseded),
  `calibrate` por livro registrado em `docs/eval.md` (CVM 0.450/24/0.80,
  IBRI 0.450/16/0.85, INVEST 0.450/28/0.75, Direito 0.550/14/0.40),
  eval full (CVM 88.65%, IBRI 89.19%, INVEST 88.63%, Direito 85.95%
  em 1993; lat Direito 447ms PASS perto do teto), gold v3 20/livro
  (80/80 recall@2; 40 novas auditadas, 5 descartadas por fora do
  domínio — usucapião/licitação/duration/guidance/silêncio).
