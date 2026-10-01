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
  - [ ] 7.6 Release: artefatos CI + `inspect --json`.
