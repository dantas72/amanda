#ifndef AMANDA_MCP_H
#define AMANDA_MCP_H

/* MCP server (Model Context Protocol) sobre stdio.
 *
 * Transporte: JSON-RPC 2.0, uma mensagem por linha em stdout/stdin
 * (newline-delimited). Tambem aceita framing estilo LSP
 * ("Content-Length: N" + cabecalhos + corpo de N bytes), usado por
 * alguns clientes MCP. Log humano vai para stderr; stdout carrega
 * SOMENTE mensagens JSON-RPC (uma por linha, terminada em \n).
 *
 * Metodos suportados:
 *   initialize              -> {protocolVersion, capabilities, serverInfo}
 *   notifications/initialized -> sem resposta (NULL)
 *   ping                    -> {}
 *   tools/list              -> [{ask, decisions, inspect, version}]
 *   tools/call              -> executa a ferramenta local (motor local
 *                              ou laya-http com fallback, conforme cfg)
 * Ferramentas:
 *   ask       {pergunta*, model?, top_k?} -> resposta + pagina/citacao
 *   decisions {pergunta*, model?}         -> JSON completo da decisao
 *   inspect   {model?}                   -> estatisticas do pacote
 *   version   {}                         -> versao do binario + formato
 * (* = obrigatorio)
 */

#include "packager.h"
#include "decision_engine.h"

#define MCP_MAX_PKGS 8
#define MCP_PROTOCOL_VERSION "2024-11-05"
#define MCP_SERVER_NAME "amandac"

typedef struct {
    AmandaPackage *pkgs[MCP_MAX_PKGS];
    RetrievalIndex *rix[MCP_MAX_PKGS];
    char names[MCP_MAX_PKGS][128];
    int n_pkgs;
    DecisionConfig base_cfg;
    int top_k;
    /* 1 = completa zeros da base_cfg com a calibracao gravada no
     * pacote de cada pergunta (v3); 0 = padrao historico (flags
     * CLI continuam valendo nos dois casos). */
    int usar_calib_pkg;
} McpCtx;

/* specs: "nome=caminho" ou "caminho" (basename vira o nome, como no
 * serve). n entre 1 e MCP_MAX_PKGS. cfg_base copiada (backend, laya,
 * calibracao). top_k <= 0 vira 3. erro_out (opcional) recebe string
 * mallocada em falha. Retorna 0 ok, != 0 erro. */
int mcp_ctx_init(McpCtx *ctx, const char **specs, int n,
                 const DecisionConfig *cfg_base, int top_k,
                 char **erro_out);
void mcp_ctx_free(McpCtx *ctx);

/* Trata UMA mensagem JSON-RPC (linha sem \n final, ou corpo de frame
 * LSP). Retorna string mallocada terminada em \n (caller libera com
 * free), ou NULL para notificacao/linha vazia (sem resposta). Erros
 * de protocolo/parametros viram resposta de erro JSON-RPC, nunca NULL
 * (exceto falta de memoria). */
char *mcp_handle_line(McpCtx *ctx, const char *line);

/* Loop stdio: le stdin, escreve respostas em stdout, log em stderr.
 * Retorna 0 ao fim do stdin (EOF) ou erro fatal de IO. */
int mcp_run(McpCtx *ctx);

#endif
