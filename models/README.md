# models/

Diretório reservado para modelos externos. **O build nunca depende
deste diretório** (`build.bat`/CMake passam sem nada aqui).

- **Núcleo (Fases 1–6): nada necessário.** Embeddings são TF locais
  (384d) e a inferência padrão é recuperação híbrida calibrada.
- **Laya (ver `docs/laya.md`):** o Laya v1.10.1 NÃO distribui GGUF.
  Ele roda como engine HTTP (`127.0.0.1:8420`) com LLMs via LiteLLM
  (Ollama local — ex. `nimble:latest` em `:11434` — ou nuvem).
  Não baixe `laya-421m.gguf` de terceiros: não é artefato oficial.
- **Opcional, se um dia houver ONNX/GGUF local:** os padrões
  ignorados são `models/*.gguf`, `models/*.onnx`, `models/*.bin`
  (ver `.gitignore`). Nenhum código atual os carrega.

Estado verificado: Ollama `nimble:latest` (9B Q8) + providers
`localollama`/`ollamaserver` no Laya; caminho vivo `laya-http`
requer o slot chat apontado para o nimble.
