@echo off
setlocal
REM sweep_typesafe.bat - curva tradeoff limiar_recusa x JEV (nuvem).
REM 4 pins (1/livro) x 5 limiares = 20 chamadas SystemOne rapidas.
REM Serve de base de testes p/ decidir um limiar padrao do JEV (ver docs/guia_jev.md 4b).
REM Requer TYPESAFE_API_KEY (env ou amandac.conf) + livros *_t74; senao SKIP (exit 0).
REM Nunca falha por tradeoff: exit 1 so em erro de infra. NUNCA exibe a chave. CRLF.
cd /d "%~dp0\.."
if not exist amandac.exe (
  echo [ERRO] amandac.exe nao encontrado. Rode build.bat primeiro.
  exit /b 1
)
set HAS_TS_KEY=0
if defined TYPESAFE_API_KEY set HAS_TS_KEY=1
findstr /r /c:"^TYPESAFE_API_KEY=" amandac.conf >nul 2>nul
if not errorlevel 1 set HAS_TS_KEY=1
if not "%HAS_TS_KEY%"=="1" (
  echo [sweep] SKIP: sem TYPESAFE_API_KEY e sem amandac.conf com a chave
  exit /b 0
)
set MISSING=0
if not exist build\cvm_teste74.amanda set MISSING=1
if not exist build\ibri_t74.amanda set MISSING=1
if not exist build\inv_t74.amanda set MISSING=1
if not exist build\dir_t74.amanda set MISSING=1
if "%MISSING%"=="1" (
  echo [sweep] SKIP: livros *_t74 ausentes em build
  exit /b 0
)
set TS_OUT=%TEMP%\amanda_sweep_out.json
echo LIMIAR  RESP  RANK_OK  RANK_OK_RESP  RECUSA_RANK_OK
for %%L in (0.30 0.50 0.70 0.85 0.89) do call :LIMIAR %%L
echo [OK] sweep concluido (4 pins x 5 limiares, recall@2).
exit /b 0
:LIMIAR
set LIM=%~1
set RESP=0
set ROK=0
set ROKR=0
set RREC=0
call :PIN build\cvm_teste74.amanda 139 "O que caracteriza uma companhia aberta?"
call :PIN build\ibri_t74.amanda 70 "O que faz a area de relacoes com investidores?"
call :PIN build\inv_t74.amanda 81 "O que e analise tecnica?"
call :PIN build\dir_t74.amanda 531 "O que e responsabilidade civil?"
echo %LIM%  %RESP%  %ROK%  %ROKR%  %RREC%
exit /b 0
:PIN
amandac.exe ask --package %~1 %~3 --json --backend typesafe-http --typesafe-model jev-latest --typesafe-timeout-ms 120000 --limiar-recusa %LIM% > "%TS_OUT%"
findstr /c:"\"backend\":\"typesafe-http\"" "%TS_OUT%" >nul 2>nul
if errorlevel 1 (
  echo [sweep] AVISO: fallback local em %~1 limiar %LIM%, pin ignorado
  exit /b 0
)
set RANK=0
findstr /C:"\"pagina\":%~2" "%TS_OUT%" >nul 2>nul
if not errorlevel 1 (
  set RANK=1
) else (
  set MARK=[p.%~2]
  findstr /C:"%MARK%" "%TS_OUT%" >nul 2>nul
  if not errorlevel 1 set RANK=1
)
if "%RANK%"=="1" set /a ROK+=1
findstr /c:"\"recusada\":false" "%TS_OUT%" >nul 2>nul
if not errorlevel 1 (
  set /a RESP+=1
  if "%RANK%"=="1" set /a ROKR+=1
) else (
  if "%RANK%"=="1" set /a RREC+=1
)
exit /b 0
