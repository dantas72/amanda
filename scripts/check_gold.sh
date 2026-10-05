#!/bin/sh
# check_gold.sh - regressao dos gold sets (POSIX). Espelha scripts/check_gold.bat.
# Roda as 80 perguntas curadas no ask local e confere "pagina".
# Semantica recall@2 (multi-citacao): PASS se a pagina esperada for a
# "pagina" OU aparecer como [p.N] na resposta.
# Sem os pacotes *_t74.amanda em build/: SKIP honesto (exit 0). Com eles:
# 80/80 PASS = exit 0; qualquer divergencia = exit 1.
# OBS: perguntas sem acento (padrao do repo).
set -eu
cd "$(dirname "$0")/.."
BIN=./amandac
if [ ! -x "$BIN" ] && [ -x ./build/amandac ]; then BIN=./build/amandac; fi
if [ ! -x "$BIN" ]; then echo "[ERRO] binario amandac nao encontrado (cmake --build build)"; exit 1; fi

if [ ! -f build/cvm_teste74.amanda ] || [ ! -f build/ibri_t74.amanda ] || \
   [ ! -f build/inv_t74.amanda ] || [ ! -f build/dir_t74.amanda ]; then
  echo "[gold] SKIP: pacotes *_t74.amanda ausentes em build -- compile os livros do pdf primeiro"
  exit 0
fi

PASS=0
FAIL=0

gold_check() {
  pkg=$1; pag=$2; shift 2; perg=$*
  out=$("$BIN" ask --package "$pkg" "$perg" --json)
  if printf '%s' "$out" | grep -q "\"pagina\":$pag"; then
    PASS=$((PASS + 1))
  elif printf '%s' "$out" | grep -qF "[p.$pag]"; then
    PASS=$((PASS + 1))
  else
    FAIL=$((FAIL + 1))
    echo "[gold] FAIL: $pkg p.$pag esperada: $perg"
    printf '%s\n' "$out"
  fi
}

echo "[gold] cvm (20)..."
gold_check build/cvm_teste74.amanda 139 "O que caracteriza uma companhia aberta?"
gold_check build/cvm_teste74.amanda 66 "O que sao valores mobiliarios?"
gold_check build/cvm_teste74.amanda 132 "O que e uma acao ordinaria?"
gold_check build/cvm_teste74.amanda 240 "Como as bolsas de valores se organizam?"
gold_check build/cvm_teste74.amanda 238 "Quais sao as instituicoes do sistema de distribuicao de valores mobiliarios?"
gold_check build/cvm_teste74.amanda 340 "O que foi a Teoria de Dow?"
gold_check build/cvm_teste74.amanda 157 "O que e governanca corporativa?"
gold_check build/cvm_teste74.amanda 374 "O que faz o profissional de RI?"
gold_check build/cvm_teste74.amanda 250 "O que e o mercado primario?"
gold_check build/cvm_teste74.amanda 317 "Como funciona a formacao de precos no mercado a vista?"
gold_check build/cvm_teste74.amanda 166 "O que e tag along?"
gold_check build/cvm_teste74.amanda 73 "O que e uma debenture?"
gold_check build/cvm_teste74.amanda 256 "O que e o Novo Mercado da B3?"
gold_check build/cvm_teste74.amanda 86 "Como funciona uma oferta publica inicial IPO?"
gold_check build/cvm_teste74.amanda 152 "O que e o Conselho Fiscal?"
gold_check build/cvm_teste74.amanda 174 "Quais sao os deveres de diligencia e lealdade dos administradores?"
gold_check build/cvm_teste74.amanda 52 "O que e o uso de informacao privilegiada no mercado de capitais?"
gold_check build/cvm_teste74.amanda 284 "O que e dividendos e juros sobre capital proprio?"
gold_check build/cvm_teste74.amanda 99 "O que e renda fixa?"
gold_check build/cvm_teste74.amanda 239 "O que e o mercado de balcao organizado?"

echo "[gold] ibri (20)..."
gold_check build/ibri_t74.amanda 70 "O que faz a area de relacoes com investidores?"
gold_check build/ibri_t74.amanda 88 "Qual a funcao da CVM?"
gold_check build/ibri_t74.amanda 92 "O que e uma companhia aberta?"
gold_check build/ibri_t74.amanda 61 "O que e divulgacao de informacoes relevantes?"
gold_check build/ibri_t74.amanda 66 "O que e governanca corporativa?"
gold_check build/ibri_t74.amanda 60 "Como a area de RI deve lidar com a imprensa?"
gold_check build/ibri_t74.amanda 94 "Quando divulgar ato ou fato relevante durante o pregao?"
gold_check build/ibri_t74.amanda 111 "O que e assembleia geral de acionistas?"
gold_check build/ibri_t74.amanda 88 "Para que serve o mercado de capitais qual sua funcao?"
gold_check build/ibri_t74.amanda 94 "O que e fato relevante?"
gold_check build/ibri_t74.amanda 111 "O que e o formulario de referencia?"
gold_check build/ibri_t74.amanda 97 "O que e o dever de sigilo do administrador?"
gold_check build/ibri_t74.amanda 92 "O que e a Instrucao CVM 358?"
gold_check build/ibri_t74.amanda 45 "Quais sao os canais de comunicacao com investidores?"
gold_check build/ibri_t74.amanda 77 "Como a area de RI deve lidar com rumores no mercado?"
gold_check build/ibri_t74.amanda 74 "Como deve ser o relacionamento com analistas de mercado?"
gold_check build/ibri_t74.amanda 103 "O que e a politica de divulgacao de informacoes da companhia?"
gold_check build/ibri_t74.amanda 111 "Como divulgar resultados trimestrais ITR DFP?"
gold_check build/ibri_t74.amanda 135 "Como preparar a abertura de capital e a area de RI?"
gold_check build/ibri_t74.amanda 74 "Quem e o responsavel pela divulgacao de informacoes na companhia?"

echo "[gold] invest (20)..."
gold_check build/inv_t74.amanda 81 "O que e analise tecnica?"
gold_check build/inv_t74.amanda 76 "O que e diversificacao de carteira?"
gold_check build/inv_t74.amanda 164 "O que e risco sistematico?"
gold_check build/inv_t74.amanda 61 "O que sao titulos sustentaveis?"
gold_check build/inv_t74.amanda 131 "O que e o principio da essencia sobre a forma?"
gold_check build/inv_t74.amanda 154 "O que e renda fixa?"
gold_check build/inv_t74.amanda 200 "O que e renda variavel?"
gold_check build/inv_t74.amanda 165 "O que e fluxo de caixa descontado?"
gold_check build/inv_t74.amanda 71 "O que foi o Fundo Europeu de Estabilidade Financeira?"
gold_check build/inv_t74.amanda 223 "Quais as metodologias mais usadas na analise de investimentos?"
gold_check build/inv_t74.amanda 205 "O que e o CAPM e o custo de capital proprio?"
gold_check build/inv_t74.amanda 77 "O que e o indice de Sharpe?"
gold_check build/inv_t74.amanda 143 "O que e analise fundamentalista?"
gold_check build/inv_t74.amanda 166 "O que e o modelo de Gordon de dividendos?"
gold_check build/inv_t74.amanda 163 "O que e o WACC custo medio ponderado de capital?"
gold_check build/inv_t74.amanda 164 "O que e taxa interna de retorno TIR?"
gold_check build/inv_t74.amanda 73 "O que e o valor presente liquido VPL?"
gold_check build/inv_t74.amanda 75 "O que e alavancagem financeira?"
gold_check build/inv_t74.amanda 76 "O que e a fronteira eficiente de Markowitz?"
gold_check build/inv_t74.amanda 78 "O que e o modelo APT de precificacao por arbitragem?"

echo "[gold] direito (20)..."
gold_check build/dir_t74.amanda 531 "O que e responsabilidade civil?"
gold_check build/dir_t74.amanda 32 "O que e a Constituicao Federal de 1988?"
gold_check build/dir_t74.amanda 530 "O que e desconsideracao da personalidade juridica?"
gold_check build/dir_t74.amanda 622 "O que e securitizacao de recebiveis?"
gold_check build/dir_t74.amanda 64 "O que e reserva de iniciativa do Presidente da Republica?"
gold_check build/dir_t74.amanda 81 "Quando ha quebra de sigilo no mercado de valores mobiliarios?"
gold_check build/dir_t74.amanda 180 "Como evoluiu a governanca das companhias aderentes?"
gold_check build/dir_t74.amanda 90 "O que diz a doutrina sobre prova em crimes complexos?"
gold_check build/dir_t74.amanda 622 "Qual a importancia da segregacao de ativos?"
gold_check build/dir_t74.amanda 168 "O que e o Codigo de Autorregulacao?"
gold_check build/dir_t74.amanda 89 "O que e o principio da dignidade da pessoa humana?"
gold_check build/dir_t74.amanda 379 "O que e a Lei das Sociedades Anonimas 6404?"
gold_check build/dir_t74.amanda 450 "O que e o direito de recesso do acionista?"
gold_check build/dir_t74.amanda 896 "O que e o poder de policia da CVM?"
gold_check build/dir_t74.amanda 1071 "O que e acao preferencial?"
gold_check build/dir_t74.amanda 396 "O que e o processo administrativo sancionador da CVM?"
gold_check build/dir_t74.amanda 393 "O que e controle societario e acionista controlador?"
gold_check build/dir_t74.amanda 705 "O que e oferta publica de distribuicao de valores mobiliarios?"
gold_check build/dir_t74.amanda 460 "O que e cisao fusao e incorporacao de sociedades?"
gold_check build/dir_t74.amanda 366 "O que e o direito de preferencia na subscricao de acoes?"

echo "[gold] resultado: $PASS PASS, $FAIL FAIL (80 perguntas)"
if [ "$FAIL" != "0" ]; then exit 1; fi
echo "[gold] OK: regressao passou."
