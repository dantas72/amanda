# Guia de utilização — Amanda (`amandac`)

Guia prático para compilar conhecimento, servir e testar. Todos os
comandos abaixo partem da raiz do repositório no **Windows (cmd)**;
no Linux/Mac troque `amandac.exe` por `./amandac` (ou `build/amandac`)
e `\` por `/`. Versão mínima: `amandac 1.0.18` (Fase 10).

## 1. Compilar e verificar a ferramenta

```bat
build.bat
amandac.exe version
```

O `build.bat` incrementa `version.bin` e gera `amandac.exe` sem
dependências externas (só `gcc` no PATH).

## 2. Fluxo mínimo (exemplo incluso)

```bat
amandac.exe compile --input examples\exemplo.txt --output build\exemplo.amanda --title "Exemplo Amanda"
amandac.exe inspect --package build\exemplo.amanda --stats
amandac.exe inspect --package build\exemplo.amanda --json
amandac.exe ask --package build\exemplo.amanda "O que e entropia?"
```

Via arquivo de configuração (precedência: flag CLI > config > padrão):

```bat
amandac.exe compile --config examples\config.yaml --output build\via_cfg.amanda
```

Entradas aceitas: `.pdf` `.txt` `.csv` `.json`.

## 3. Um `.amanda` por livro (especializações)

Cada PDF gera seu próprio artefato, com páginas e citações daquela
fonte. Não junte livros num `.amanda` só (mistura fontes e derruba
a fidelidade). Com os livros em `pdf\`:

```bat
amandac.exe compile --input "pdf\CVM-livro_top_valores_mobiliarios_br_5ed.pdf" --output build\cvm.amanda
amandac.exe compile --input "pdf\Livro-IBRI-CVM.pdf" --output build\ibri.amanda
amandac.exe compile --input "pdf\top-analise-de-investimentos-2ed.pdf" --output build\invest.amanda
amandac.exe compile --input "pdf\livro_top_direito.pdf" --output build\direito.amanda
```

O `compile` imprime a cobertura da extração
(`streams`, `texto`, `falhas`, `fallback`). `falhas` deve ser 0;
`fallback=sim` indica PDF problemático (texto parcial).

Referência medida (`amandac` 1.0.15–1.0.17):

| pacote | blocos/páginas | perguntas | fidelidade@10–20% |
|---|---|---|---|
| cvm | 448/412 | 5320 | 86% |
| ibri | 178/161 | 2415 | 90% |
| invest | 253/260 | 2498 | 90% |
| direito | 1393/1348 | 33218 | ver § 6 |

## 4. Avaliar e calibrar (fecha o loop)

```bat
amandac.exe eval --package build\cvm.amanda --sample 0.1
amandac.exe calibrate --package build\cvm.amanda --sample 0.2
```

O `calibrate` sugere `--conf-center/--conf-slope/--limiar-recusa`.
Reavalie com os valores sugeridos e adote se `bal` subir e as probes
(`recusa fora-escopo`) fecharem em 3/3:

```bat
amandac.exe eval --package build\cvm.amanda --sample 0.1 --conf-center 0.45 --conf-slope 26 --limiar-recusa 0.70
amandac.exe eval --package build\cvm.amanda --sample 0.1 --json
```

O relatório traz `cobertura` (páginas, blocos, chunks, chars,
streams, falhas). Progresso de amostras grandes sai em `stderr`
(o `stdout` fica limpo para `--json`).

## 5. Servir (API compatível OpenAI)

```bat
amandac.exe serve --package build\cvm.amanda --port 8080
```

Flags úteis (Fases 7.2/7.5):

| flag | default | efeito |
|---|---|---|
| `--conf-center/--conf-slope/--limiar-recusa` | 0.12/12.0/0.30 | calibração do `serve` |
| `--cors ORIGEM` | `*` | `Access-Control-Allow-Origin` |
| `--api-key CHAVE` | aberto | exige `Authorization: Bearer CHAVE` |
| `--max-body BYTES` | 1048576 | acima = `413` |
| `--max-conns N` | 16 | cheio = `503` + `Retry-After` |
| `--eval-max N` | 200 | teto do `POST /v1/eval` (excedeu = `400`) |

Rotas: `GET /v1/models`, `GET /v1/amanda/info`,
`POST /v1/chat/completions` (com `"stream": true` para SSE),
`POST /v1/decisions`, `POST /v1/embeddings`, `POST /v1/eval`.

Testes rápidos com `curl.exe` (fixtures em `examples\`):

```bat
curl.exe -s http://127.0.0.1:8080/v1/models
curl.exe -s -X POST http://127.0.0.1:8080/v1/chat/completions -H "Content-Type: application/json" -d "@examples\smoke_chat.json"
curl.exe -s -X POST http://127.0.0.1:8080/v1/decisions -H "Content-Type: application/json" -d "@examples\smoke_decisions.json"
curl.exe -s -X POST http://127.0.0.1:8080/v1/embeddings -H "Content-Type: application/json" -d "@examples\smoke_embeddings.json"
curl.exe -s -X POST http://127.0.0.1:8080/v1/eval -H "Content-Type: application/json" -d "@examples\smoke_eval.json"
```

Com chave (troque `TESTE123` pela sua):

```bat
amandac.exe serve --package build\cvm.amanda --port 8080 --api-key TESTE123
curl.exe -s http://127.0.0.1:8080/v1/models
curl.exe -s -H "Authorization: Bearer TESTE123" http://127.0.0.1:8080/v1/models
```

## 6. Bases gigantes (livro de Direito)

Com 33 mil perguntas, a amostra padrão estoura o tempo. Limite:

```bat
amandac.exe eval --package build\direito.amanda --sample 0.05 --max-amostras 200
amandac.exe calibrate --package build\direito.amanda --sample 0.02
```

No `serve`, o `POST /v1/eval` já barra acima de `--eval-max`
(`400` pedindo `sample` menor) — é proteção, não erro.

## 7. Ajuste fino da extração PDF (Fase 10)

Só mexa se o texto sair com palavras grudadas ou separadas
(ex.: `C o u n c i l` ou `dosvalores`). Os limiares estão em
milésimos de corpo (já relativos à fonte):

```bat
amandac.exe compile --input livro.pdf --output livro.amanda --tj-espaco -100 --tj-salto 500
```

Ou na seção `extracao:` do YAML (`tj_espaco`, `tj_salto`).
Depois de mudar, rode `calibrate` de novo (o wording muda os scores).

## 8. Bateria completa (o que o CI roda)

```bat
build.bat
gcc -O2 -std=c11 -Iinclude tests/test_all.c src/amanda.c src/utils.c src/pdf_extractor.c src/chunker.c src/embedder.c src/question_gen.c src/decision_engine.c src/laya_backend.c src/packager.c src/server.c src/eval.c src/calibra.c src/config.c -o build/amanda_tests.exe -lws2_32 && build/amanda_tests.exe
scripts\test_pipeline.bat
```

Referência: **108 checks** unitários + 8 etapas de integração
(2 etapas Laya com SKIP honesto sem o engine em `:8420`).

## 9. Problemas comuns

| sintoma | causa provável | o que fazer |
|---|---|---|
| `401` no `serve` | faltou `Authorization: Bearer` | passe a chave de `--api-key` |
| `413` | corpo acima de `--max-body` | aumente o limite ou reduza o `input` |
| `503` | mais de `--max-conns` simultâneas | aguarde (`Retry-After: 2`) ou aumente |
| `400` no `/v1/eval` citando teto | amostra > `--eval-max` | use `sample` menor |
| `fallback=sim` no compile | PDF escaneado/imagem | sem texto extraível; use OCR antes |
| etapa Laya SKIP | engine fora do ar | normal sem o Laya; ver `docs/laya.md` |
| CI macOS ausente | runner ARM com falha desde 7.2 | Windows+Linux cobrem; volta na Fase 10 com diagnóstico em Mac real |
