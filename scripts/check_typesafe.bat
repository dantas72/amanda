@echo off
setlocal
REM check_typesafe.bat - pipeline real JEV/TypeSafe (nimble local + nuvem).
REM 1. nimble no Ollama :11434, sem chave: ask com julgamento JEV real.
REM 2. nuvem api.typesafe.ai, com TYPESAFE_API_KEY: 1 ask + 1 pin por livro.
REM SKIP honesto por etapa. NUNCA exibe a chave. CRLF.
cd /d "%~dp0\.."
if not exist amandac.exe (
  echo [ERRO] amandac.exe nao encontrado. Rode build.bat primeiro.
  exit /b 1
)
if not exist build\exemplo.amanda (
  echo [typesafe] compilando pacote de teste...
  amandac.exe compile --input examples\exemplo.txt --output build\exemplo.amanda --title "Exemplo Amanda"
  if errorlevel 1 exit /b 1
)
set TS_OUT=%TEMP%\amanda_ts_out.json
set HAS_TS_KEY=0
if defined TYPESAFE_API_KEY set HAS_TS_KEY=1
findstr /r /c:"^TYPESAFE_API_KEY=" amandac.conf >nul 2>nul
if not errorlevel 1 set HAS_TS_KEY=1
set HAS_NIMBLE=0
curl.exe -s --max-time 5 http://127.0.0.1:11434/api/tags -o "%TEMP%\amanda_ollama.json" 2>nul
findstr /c:"nimble" "%TEMP%\amanda_ollama.json" >nul 2>nul
if not errorlevel 1 set HAS_NIMBLE=1
if "%HAS_NIMBLE%"=="1" goto NIMBLE
echo [typesafe] SKIP: nimble indisponivel no Ollama :11434
goto CLOUD
:NIMBLE
echo [typesafe] nimble local: ask com julgamento JEV real...
amandac.exe ask --package build\exemplo.amanda "O que e entropia?" --json --backend typesafe-http --typesafe-url http://127.0.0.1:11434 --typesafe-model nimble --typesafe-timeout-ms 300000 > "%TS_OUT%"
if errorlevel 1 exit /b 1
findstr /c:"\"backend\":\"typesafe-http\"" "%TS_OUT%" >nul
if errorlevel 1 (
  echo [typesafe] FALHA: nimble nao julgou, caiu para local
  exit /b 1
)
echo [typesafe] nimble local: OK, julgamento JEV com grounding local
:CLOUD
if not "%HAS_TS_KEY%"=="1" (
  echo [typesafe] SKIP: sem TYPESAFE_API_KEY e sem amandac.conf com a chave, sem teste nuvem
  goto BOOKS
)
echo [typesafe] nuvem api.typesafe.ai com jev-latest...
amandac.exe ask --package build\exemplo.amanda "O que e entropia?" --json --backend typesafe-http --typesafe-model jev-latest --typesafe-timeout-ms 120000 > "%TS_OUT%"
if errorlevel 1 exit /b 1
findstr /c:"\"backend\":\"typesafe-http\"" "%TS_OUT%" >nul
if errorlevel 1 (
  echo [typesafe] FALHA: nuvem nao julgou, confira chave/modelo/rede
  exit /b 1
)
echo [typesafe] nuvem: OK
:BOOKS
if not "%HAS_TS_KEY%"=="1" (
  echo [typesafe] livros: SKIP sem nuvem, nimble local e lento demais por pergunta
  echo [OK] typesafe smoke passou.
  exit /b 0
)
set TPASS=0
set TFAIL=0
echo [typesafe] livros com jev-latest, 1 pin por livro...
call :GCHECK build\cvm_teste74.amanda 139 "O que caracteriza uma companhia aberta?"
call :GCHECK build\ibri_t74.amanda 70 "O que faz a area de relacoes com investidores?"
call :GCHECK build\inv_t74.amanda 81 "O que e analise tecnica?"
call :GCHECK build\dir_t74.amanda 531 "O que e responsabilidade civil?"
echo [typesafe] livros: %TPASS% PASS, %TFAIL% FAIL, 4 pins
if not "%TFAIL%"=="0" exit /b 1
echo [OK] typesafe smoke passou.
exit /b 0
:GCHECK
if not exist %~1 (
  echo [typesafe] SKIP livro ausente: %~1
  exit /b 0
)
amandac.exe ask --package %~1 %~3 --json --backend typesafe-http --typesafe-model jev-latest --typesafe-timeout-ms 120000 > "%TS_OUT%"
findstr /C:"\"pagina\":%~2" "%TS_OUT%" >nul 2>nul
if not errorlevel 1 (
  set /a TPASS+=1
  exit /b 0
)
set MARK=[p.%~2]
findstr /C:"%MARK%" "%TS_OUT%" >nul 2>nul
if errorlevel 1 (
  set /a TFAIL+=1
  echo [typesafe] FAIL: %~1 p.%~2 esperada: %~3
  type "%TS_OUT%"
) else (
  set /a TPASS+=1
)
exit /b 0
