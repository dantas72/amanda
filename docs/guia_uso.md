# Guia de utilização — Amanda (`amandac`)

Guia prático para compilar conhecimento, servir e testar. Todos os
comandos abaixo partem da raiz do repositório no **Windows (cmd)**;
no Linux/Mac troque `amandac.exe` por `./amandac` (ou `build/amandac`)
e `\` por `/`. Versão mínima: `amandac 1.0.34` (Fase 13).

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

Referência medida (`amandac` 1.0.34, pacotes v3 com calibração aplicada):

| pacote | blocos/páginas | perguntas | fidelidade | lat média |
|---|---|---|---|---|
| cvm | 448/412 | 5320 | 86.64% | 0.7 ms |
| ibri | 178/161 | 2415 | 87.41% | 0.3 ms |
| invest | 253/260 | 2498 | 88.27% | 0.3 ms |
| direito | 1393/1348 | 33218 | 84.00% | 5.3 ms |

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

Para gravar de vez no pacote (formato v3 — pacotes antigos precisam
recompilar, sem migração enquanto a ferramenta evolui):

```bat
amandac.exe calibrate --package build\cvm.amanda --sample 0.2 --apply
amandac.exe calibrate --package build\cvm.amanda --sample 0.2 --apply --output build\cvm_cal.amanda
```

Com calibração gravada, `ask`/`eval`/`serve` usam-na automaticamente;
passe as flags para sobrescrever, ou `--ignore-calib` para forçar o
padrão histórico (0.12/12.0/0.30).

Com validação natural (gold do livro — evita que o limiar suba à
custa de perguntas reais; o objetivo vira `(bal + recall@2)/2`):

```bat
amandac.exe calibrate --package build\cvm.amanda --sample 0.2 --validacao examples\gold_cvm.json --apply
```

Referência aplicada (`amandac` 1.0.32+, pacotes v3 em `build/`):

| pacote | center/slope/limiar | bal | recall@2 val |
|---|---|---|---|
| cvm | 0.200/16.0/0.85 | 0.995 | 19/20 |
| ibri | 0.200/22.0/0.89 | 1.000 | 18/20 |
| invest | 0.250/30.0/0.82 | 0.999 | 16/20 |
| direito | 0.250/30.0/0.89 | 0.999 | 19/20 |

O relatório traz `cobertura` (páginas, blocos, chunks, chars,
streams, falhas). Progresso de amostras grandes sai em `stderr`
(o `stdout` fica limpo para `--json`).

## 5. Servir (API compatível OpenAI)

```bat
amandac.exe serve --package build\cvm.amanda --port 8080
```

Flags úteis (Fases 7.2/7.5/13):

| flag | default | efeito |
|---|---|---|
| `--conf-center/--conf-slope/--limiar-recusa` | 0.12/12.0/0.30 | calibração do `serve` |
| `--cors ORIGEM` | `*` | `Access-Control-Allow-Origin` |
| `--api-key CHAVE` / `AMANDA_API_KEY` / `--api-key-file ARQ` | aberto | exige `Authorization: Bearer CHAVE` |
| `--max-body BYTES` | 1048576 | acima = `413` |
| `--max-conns N` | 16 | fila+ativas; cheio = `503` + `Retry-After` |
| `--workers N` | 8 | pool fixo de threads (teto 64) |
| `--eval-max N` | 200 | teto do `POST /v1/eval` (excedeu = `400`) |
| `--config ARQ` | — | seção `servidor:` (flag CLI prevalece) |

Via arquivo de configuração:

```bat
amandac.exe serve --config examples\config.yaml
```

Multi-pacote (um `.amanda` por livro, uma porta):

```bat
amandac.exe serve --package cvm=build\cvm_teste74.amanda --package ibri=build\ibri_t74.amanda --port 8080
curl.exe -s http://127.0.0.1:8080/v1/models
```

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

Com chave (troque `TESTE123` pela sua; prefira env ou arquivo —
flag vaza em `ps`):

```bat
amandac.exe serve --package build\cvm.amanda --port 8080 --api-key TESTE123
curl.exe -s http://127.0.0.1:8080/v1/models
curl.exe -s -H "Authorization: Bearer TESTE123" http://127.0.0.1:8080/v1/models
```

## 5.1 Servir com LLM via Laya (Fase 11)

Exige o engine do Laya em `:8420` com slot chat apontado p/ modelo
com provider (ver `docs/laya.md`). Sem isso, tudo cai em `local`:

```bat
amandac.exe serve --package build\cvm.amanda --port 8080 --backend laya-http --laya-max 2
curl.exe -s -X POST http://127.0.0.1:8080/v1/chat/completions -H "Content-Type: application/json" -d "@examples\smoke_chat.json"
```

A resposta traz `"backend":"laya-http"` (ou `"local"` no fallback) e,
no caminho vivo, `" (via Laya)"` no texto. `--laya-max` protege o
engine; `--laya-timeout-ms` (default 60000) limita cada inferência.

## 6. Bases gigantes (livro de Direito)

Com 33 mil perguntas, prefira amostras limitadas para iterar rápido
(o índice 12.4 já responde em ~5ms mesmo no Direito):

```bat
amandac.exe eval --package build\direito.amanda --sample 0.05 --max-amostras 200
amandac.exe calibrate --package build\direito.amanda --sample 0.02 --validacao examples\gold_direito.json --apply
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

Referência: **181 checks** unitários + 8 etapas de integração
(2 etapas Laya com SKIP honesto sem o engine em `:8420`)
+ regressão gold (`scripts\check_gold.bat`, 80 perguntas, recall@2).

## 9. Problemas comuns

| sintoma | causa provável | o que fazer |
|---|---|---|
| `401` no `serve` | faltou `Authorization: Bearer` | passe a chave de `--api-key` |
| `413` | corpo acima de `--max-body` | aumente o limite ou reduza o `input` |
| `503` | mais de `--max-conns` simultâneas | aguarde (`Retry-After: 2`) ou aumente |
| `400` no `/v1/eval` citando teto | amostra > `--eval-max` | use `sample` menor |
| `fallback=sim` no compile | PDF escaneado/imagem | sem texto extraível; use OCR antes |
| etapa Laya SKIP | engine fora do ar | normal sem o Laya; ver `docs/laya.md` |
| CI macOS | runners M1 ARM + Intel ativos desde a Fase 14 | M2+ só via self-hosted |
| sem HTTPS próprio | `serve` é HTTP puro | rode atrás de reverse-proxy (nginx/Caddy) em produção |
| log de acesso | vai para `stderr` | hora, método, rota, código, ms (sem corpo nem chave) |
