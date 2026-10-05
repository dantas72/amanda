# Backend DeepSeek — `amandac --backend deepseek-http`

Redação via nuvem DeepSeek (chat OpenAI-compatible) sobre o grounding
local: mesma citação/página do motor local, texto redigido pelo
`deepseek-chat`. Qualquer falha = fallback local honesto (campo
`"backend"` informa).

## Chave (nunca no repo, nunca logada)

Precedência: `--deepseek-key` > env `DEEPSEEK_API_KEY` >
`--deepseek-key-file` > `amandac.conf` (`KEY=valor`, gitignored;
modelo em `examples/amandac.conf`).

```powershell
$env:DEEPSEEK_API_KEY = "sua-chave"
```

## Uso

```sh
amandac ask --package livro.amanda "Pergunta" --json \
  --backend deepseek-http --deepseek-model deepseek-chat
amandac serve --package livro.amanda --port 8080 --backend deepseek-http
```

Defaults: URL `https://api.deepseek.com`, modelo `deepseek-chat`,
timeout 120s (`--deepseek-url/--deepseek-model/
--deepseek-timeout-ms`, `serve --config`, `amanda.json` e Docker
com `DEEPSEEK_URL/_MODEL/_TIMEOUT_MS`). `eval`/`mcp` aceitam as
mesmas flags.

## Pipeline

```bat
scripts\check_deepseek.bat
```

```sh
sh scripts/check_deepseek.sh
```

1 `ask` exigindo `"backend":"deepseek-http"`; SKIP sem
`DEEPSEEK_API_KEY`.
