@echo off
setlocal
REM check_deepseek.bat - redacao real via DeepSeek (nuvem, com DEEPSEEK_API_KEY).
REM Sem chave: SKIP honesto. NUNCA exibe a chave. CRLF.
cd /d "%~dp0\.."
if not exist amandac.exe (
  echo [ERRO] amandac.exe nao encontrado. Rode build.bat primeiro.
  exit /b 1
)
set HAS_DS_KEY=0
if defined DEEPSEEK_API_KEY set HAS_DS_KEY=1
findstr /r /c:"^DEEPSEEK_API_KEY=" amandac.conf >nul 2>nul
if not errorlevel 1 set HAS_DS_KEY=1
if "%HAS_DS_KEY%"=="1" goto ASK
echo [deepseek] SKIP: sem DEEPSEEK_API_KEY e sem amandac.conf com a chave
exit /b 0
:ASK
if not exist build\exemplo.amanda (
  echo [deepseek] compilando pacote de teste...
  amandac.exe compile --input examples\exemplo.txt --output build\exemplo.amanda --title "Exemplo Amanda"
  if errorlevel 1 exit /b 1
)
set DS_OUT=%TEMP%\amanda_ds_out.json
echo [deepseek] nuvem api.deepseek.com com deepseek-chat...
amandac.exe ask --package build\exemplo.amanda "O que e entropia?" --json --backend deepseek-http --deepseek-model deepseek-chat --deepseek-timeout-ms 120000 > "%DS_OUT%"
if errorlevel 1 exit /b 1
findstr /c:"\"backend\":\"deepseek-http\"" "%DS_OUT%" >nul
if errorlevel 1 (
  echo [deepseek] FALHA: nuvem nao redigiu, confira chave/modelo/rede
  exit /b 1
)
echo [deepseek] nuvem: OK
echo [OK] deepseek smoke passou.
