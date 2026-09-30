# models/

Diretório reservado para modelos externos (Fase 3+).

- **Fase 1 não precisa de nada aqui.** Embeddings são hash-TF-IDF
  locais (384d) e a inferência é recuperação híbrida calibrada.
- **Laya (ver `docs/laya.md`):** o Laya v1.10.1 NÃO distribui GGUF.
  Ele roda como engine HTTP (`127.0.0.1:8420`) com LLMs via
  LiteLLM (Ollama/LM Studio local ou Claude/GPT/Gemini).
  Não baixe `laya-421m.gguf` de terceiros — não é artefato oficial.
- Futuro (Fase 3): aqui poderão viver `embedding-model.onnx`
  ou GGUFs genéricos para llama.cpp — sempre opcionais, nunca
  obrigatórios para `build.bat`.
