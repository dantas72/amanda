# FASES — Projeto Amanda (controle de implementação)

- [x] Fase 1 — Núcleo Windows em C puro (`amandac.exe`): extractor PDF/TXT/CSV/JSON, chunker, embedder 384d, question_gen (choice/score/noul), decision engine híbrido, packager `.amanda` com CRC, server HTTP OpenAI-compatible, CLI compile/serve/ask/inspect/version, `build.bat`, `version.bin` 1.0.1, testes unitários + integração.
- [ ] Fase 2 — CMake multiplataforma validado em Linux/Mac + CI.
- [ ] Fase 3 — (DEFERIDA: só após Fases 1–2 do Projeto.md) Integração Laya via HTTP, sem GGUF: `amandac serve` como backend OpenAI-compatible do Laya (LiteLLM) + backend opcional `laya-http` no decision_engine com fallback local. Pré-requisito: Laya instalado (engine em `127.0.0.1:8420`). Até lá, a etapa 5/5 da pipeline (`scripts\check_laya.bat`) retorna SKIP sem falhar. Ver `docs/laya.md`.
- [ ] Fase 4 — Servidor com streaming SSE + endpoint `/v1/embeddings`.
- [ ] Fase 5 — Calibração com validação em 10% das perguntas + métricas (fidelidade/calibração/latência/recusa).
