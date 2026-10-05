# MCP server — `amandac mcp`

Servidor [Model Context Protocol](https://modelcontextprotocol.io) sobre
**stdio** (JSON-RPC 2.0, uma mensagem por linha). Expõe o motor local
de decisão (ou `laya-http` com fallback) como ferramentas para
OpenCode, Claude Desktop e qualquer cliente MCP — sem rede, sem
dependências novas (C11 puro, mesmo binário).

Protocolo: `MCP_PROTOCOL_VERSION 2024-11-05`
(`include/mcp.h`, `src/mcp.c`, testes em `test_mcp`).

## Uso

```sh
amandac mcp --package exemplo.amanda
amandac mcp --package cvm=livro1.amanda --package ibri=livro2.amanda --top-k 3
amandac mcp --package exemplo.amanda --backend laya-http --laya-url http://127.0.0.1:8420
```

Flags: `--top-k`, `--conf-center`, `--conf-slope`, `--limiar-recusa`,
`--ignore-calib` (mesma semântica do `ask`; calibração v3 do pacote
vale por pacote, flag CLI prevalece).

`stdout` carrega **somente** JSON-RPC (uma resposta por linha);
log humano vai para `stderr`. Também aceita framing estilo LSP
(`Content-Length: N` + corpo), para clientes que o enviam.

## Ferramentas

| ferramenta | argumentos | retorno |
|---|---|---|
| `ask` | `pergunta`* `model?` `top_k?` | texto: resposta + `[confianca pagina backend]` + citação; recusa fora de escopo (não é erro) |
| `decisions` | `pergunta`* `model?` | JSON cru: `resposta probabilidade confianca pagina citacao recusada backend` |
| `inspect` | `model?` | JSON de `inspect --json` (chunks, perguntas, formato, calibração) |
| `version` | — | `amandac X.Y.Z (formato .amanda vN)` |

`*` obrigatório. `model` = nome do `--package` (`nome=caminho`;
omitido/`"amanda"` = primeiro). `model` desconhecido responde
`isError:true` com a lista de pacotes (nunca inventa pacote).

Métodos MCP: `initialize` (capacidades + versão), `ping`,
`tools/list`, `tools/call`, `notifications/*` (sem resposta).
Erros JSON-RPC padrão: `-32700` parse, `-32601` método desconhecido,
`-32602` parâmetro/ferramenta inválida (sempre com o `id` ecoado
verbatim, número ou string).

## Configurar clientes

`examples/mcp_config.json` é o modelo (troque os `CAMINHO_*`):

```json
{ "mcpServers": {
    "amanda": { "command": "CAMINHO_AMANDAC/amandac",
                "args": ["mcp", "--package", "CAMINHO_PACOTE/exemplo.amanda"] } } }
```

OpenCode: `opencode.json` → `"mcp": { "amanda": { "type": "local",
"command": ["amandac", "mcp", "--package", "..."], "enabled": true } }`.

## Teste manual

```bat
scripts\check_mcp.bat
```

```sh
sh scripts/check_mcp.sh
```

Exercita initialize → tools/list → ask/decisions/version/ping +
`-32602`, e valida que a notificação não gera resposta.
