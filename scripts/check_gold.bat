@echo off
setlocal
REM check_gold.bat - regressao dos gold sets (examples/gold_*.json)
REM Roda as 40 perguntas curadas no ask local e confere "pagina".
REM Semantica recall@2 (Fase 12.2, multi-citacao): PASS se a pagina
REM esperada for a "pagina" OU aparecer como [p.N] na resposta
REM (as 2 fontes vao para o usuario; ex. responsabilidade civil 531
REM em 2o). Gold v2: 20 pins atualizados p/ BM25+stopwords (auditados
REM em FASES 12.2), 4 mantidos via 2a citacao, 16 inalterados.
REM Sem os pacotes *_t74.amanda: SKIP honesto (exit 0). Com eles:
REM 40/40 PASS = exit 0; qualquer divergencia = exit 1.
REM OBS: perguntas sem acento (padrao do repo); nao usar PowerShell aqui.
cd /d "%~dp0\.."

if not exist amandac.exe (
  echo [ERRO] amandac.exe nao encontrado. Rode build.bat primeiro.
  exit /b 1
)
set MISSING=0
if not exist build\cvm_teste74.amanda set MISSING=1
if not exist build\ibri_t74.amanda set MISSING=1
if not exist build\inv_t74.amanda set MISSING=1
if not exist build\dir_t74.amanda set MISSING=1
if "%MISSING%"=="1" (
  echo [gold] SKIP: pacotes *_t74.amanda ausentes em build -- compile os livros do pdf primeiro
  exit /b 0
)

set PASS=0
set FAIL=0

echo [gold] cvm (10)...
call :CHECK build\cvm_teste74.amanda 139 "O que caracteriza uma companhia aberta?"
call :CHECK build\cvm_teste74.amanda 49 "O que sao valores mobiliarios?"
call :CHECK build\cvm_teste74.amanda 70 "O que e uma acao ordinaria?"
call :CHECK build\cvm_teste74.amanda 240 "Como as bolsas de valores se organizam?"
call :CHECK build\cvm_teste74.amanda 111 "Qual a funcao da CVM?"
call :CHECK build\cvm_teste74.amanda 340 "O que foi a Teoria de Dow?"
call :CHECK build\cvm_teste74.amanda 157 "O que e governanca corporativa?"
call :CHECK build\cvm_teste74.amanda 374 "O que faz o profissional de RI?"
call :CHECK build\cvm_teste74.amanda 250 "O que e o mercado primario?"
call :CHECK build\cvm_teste74.amanda 291 "Como funciona a formacao de precos no mercado a vista?"

echo [gold] ibri (10)...
call :CHECK build\ibri_t74.amanda 45 "O que e relacao com investidores?"
call :CHECK build\ibri_t74.amanda 88 "Qual a funcao da CVM?"
call :CHECK build\ibri_t74.amanda 92 "O que e uma companhia aberta?"
call :CHECK build\ibri_t74.amanda 61 "O que e divulgacao de informacoes relevantes?"
call :CHECK build\ibri_t74.amanda 66 "O que e governanca corporativa?"
call :CHECK build\ibri_t74.amanda 60 "Como a area de RI deve lidar com a imprensa?"
call :CHECK build\ibri_t74.amanda 94 "Quando divulgar ato ou fato relevante durante o pregao?"
call :CHECK build\ibri_t74.amanda 112 "O que e assembleia geral de acionistas?"
call :CHECK build\ibri_t74.amanda 41 "O que e o mercado de capitais?"
call :CHECK build\ibri_t74.amanda 94 "O que e fato relevante?"

echo [gold] invest (10)...
call :CHECK build\inv_t74.amanda 81 "O que e analise tecnica?"
call :CHECK build\inv_t74.amanda 76 "O que e diversificacao de carteira?"
call :CHECK build\inv_t74.amanda 164 "O que e risco sistematico?"
call :CHECK build\inv_t74.amanda 61 "O que sao titulos sustentaveis?"
call :CHECK build\inv_t74.amanda 131 "O que e o principio da essencia sobre a forma?"
call :CHECK build\inv_t74.amanda 154 "O que e renda fixa?"
call :CHECK build\inv_t74.amanda 200 "O que e renda variavel?"
call :CHECK build\inv_t74.amanda 165 "O que e fluxo de caixa descontado?"
call :CHECK build\inv_t74.amanda 71 "O que foi o Fundo Europeu de Estabilidade Financeira?"
call :CHECK build\inv_t74.amanda 223 "Quais as metodologias mais usadas na analise de investimentos?"

echo [gold] direito (10)...
call :CHECK build\dir_t74.amanda 531 "O que e responsabilidade civil?"
call :CHECK build\dir_t74.amanda 64 "O que e a Constituicao Federal de 1988?"
call :CHECK build\dir_t74.amanda 530 "O que e desconsideracao da personalidade juridica?"
call :CHECK build\dir_t74.amanda 648 "O que e securitizacao de recebiveis?"
call :CHECK build\dir_t74.amanda 64 "O que e reserva de iniciativa do Presidente da Republica?"
call :CHECK build\dir_t74.amanda 81 "Quando ha quebra de sigilo no mercado de valores mobiliarios?"
call :CHECK build\dir_t74.amanda 180 "Como evoluiu a governanca das companhias aderentes?"
call :CHECK build\dir_t74.amanda 90 "O que diz a doutrina sobre prova em crimes complexos?"
call :CHECK build\dir_t74.amanda 622 "Qual a importancia da segregacao de ativos?"
call :CHECK build\dir_t74.amanda 179 "O que e o Codigo de Autorregulacao?"

echo [gold] resultado: %PASS% PASS, %FAIL% FAIL (40 perguntas)
if not "%FAIL%"=="0" exit /b 1
echo [gold] OK: regressao passou.
exit /b 0

:CHECK
amandac.exe ask --package %~1 %~3 --json > "%TEMP%\gold_q.json"
findstr /C:"\"pagina\":%~2" "%TEMP%\gold_q.json" >nul 2>nul
if not errorlevel 1 (
  set /a PASS+=1
  exit /b 0
)
REM recall@2 (Fase 12.2): pagina esperada como 2a citacao [p.N]
set MARK=[p.%~2]
findstr /C:"%MARK%" "%TEMP%\gold_q.json" >nul 2>nul
if errorlevel 1 (
  set /a FAIL+=1
  echo [gold] FAIL: %~1 p.%~2 esperada: %~3
  type "%TEMP%\gold_q.json"
) else (
  set /a PASS+=1
)
exit /b 0
