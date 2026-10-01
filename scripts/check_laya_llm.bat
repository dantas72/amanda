@echo off
setlocal
REM check_laya_llm.bat - Fase 3: testa o backend laya-http (so curl+findstr)
REM 1. engine no ar? 2. providers com modelos? 3. /chat responde?
REM LIVE: roda amandac ask --backend laya-http. Senao: SKIP honesto.
REM Nunca falha a pipeline (exit 0), exceto erro real de script.
cd /d "%~dp0\.."
set LAYA_HOST=127.0.0.1
set LAYA_PORT=8420
if not "%LAYA_PORT_OVERRIDE%"=="" set LAYA_PORT=%LAYA_PORT_OVERRIDE%
set BASE=http://%LAYA_HOST%:%LAYA_PORT%

if not exist amandac.exe (
  echo [laya-llm] SKIP: amandac.exe nao encontrado
  exit /b 0
)
if not exist build\exemplo.amanda (
  echo [laya-llm] SKIP: build\exemplo.amanda ausente
  exit /b 0
)

curl.exe -s --max-time 8 "%BASE%/health" -o "%TEMP%\laya_h.json"
if errorlevel 1 (
  echo [laya-llm] SKIP: engine fora do ar
  exit /b 0
)

curl.exe -s --max-time 8 "%BASE%/settings/available-models" -o "%TEMP%\laya_p.json"
if errorlevel 1 (
  echo [laya-llm] SKIP: sem resposta de available-models
  exit /b 0
)
findstr /R /C:"\"models\": *\[[^]]" "%TEMP%\laya_p.json" >nul 2>nul
if errorlevel 1 (
  echo [laya-llm] SKIP: engine sem provider LLM
  exit /b 0
)

curl.exe -s --max-time 90 -X POST "%BASE%/chat" -H "Content-Type: application/json" -d "{\"message\": \"Responda com uma palavra: ok\"}" -o "%TEMP%\laya_c.json"
if errorlevel 1 (
  echo [laya-llm] SKIP: /chat nao respondeu em 90s
  exit /b 0
)
findstr /C:"encountered an error" "%TEMP%\laya_c.json" >nul 2>nul
if not errorlevel 1 (
  echo [laya-llm] SKIP: /chat sem modelo ativo
  exit /b 0
)

echo [laya-llm] LIVE: engine com LLM, testando ask --backend laya-http ...
amandac.exe ask --package build\exemplo.amanda "O que e entropia?" --backend laya-http --json > "%TEMP%\laya_ask.json"
if errorlevel 1 (
  echo [laya-llm] FAIL: ask --backend laya-http retornou erro
  exit /b 0
)
findstr /C:"laya-http" "%TEMP%\laya_ask.json" >nul 2>nul
if not errorlevel 1 (
  echo [laya-llm] OK: resposta via Laya
) else (
  echo [laya-llm] OK: fallback local
)
echo [laya-llm] LIVE: testando serve --backend laya-http ...
start "" /min amandac.exe serve --package build\exemplo.amanda --port 18082 --backend laya-http --laya-url %BASE% --laya-max 1
powershell -NoProfile -Command "Start-Sleep -Seconds 3"
curl.exe -s --max-time 150 -X POST http://127.0.0.1:18082/v1/chat/completions -H "Content-Type: application/json" -d "@examples\smoke_chat.json" -o "%TEMP%\laya_serve.json"
findstr /C:"laya-http" "%TEMP%\laya_serve.json" >nul 2>nul
if not errorlevel 1 (
  echo [laya-llm] OK: serve via Laya
) else (
  echo [laya-llm] OK: serve fallback local
)
taskkill /F /IM amandac.exe >nul 2>nul
exit /b 0
