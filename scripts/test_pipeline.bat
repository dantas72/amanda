@echo off
setlocal
REM Teste de integracao: compile -> inspect -> ask -> serve
cd /d "%~dp0\.."
if not exist amandac.exe (
  echo [ERRO] amandac.exe nao encontrado. Rode build.bat primeiro.
  exit /b 1
)
echo [1/4] compile...
amandac.exe compile --input examples\exemplo.txt --output build\exemplo.amanda --title "Exemplo Amanda" || exit /b 1
echo [1b/4] compile via --config...
amandac.exe compile --config examples\config.yaml --output build\exemplo_cfg.amanda || exit /b 1
amandac.exe inspect --package build\exemplo_cfg.amanda --stats || exit /b 1
del build\exemplo_cfg.amanda
echo [2/4] inspect...
amandac.exe inspect --package build\exemplo.amanda --stats || exit /b 1
amandac.exe inspect --package build\exemplo.amanda --json > "%TEMP%\amanda_inspect.json" || exit /b 1
powershell -NoProfile -Command "$t = Get-Content $env:TEMP\amanda_inspect.json -Raw; if (-not $t.Contains('chunks')) { Write-Host 'inspect-json: SEM chunks'; exit 1 } else { Write-Host 'inspect-json: OK' }"
if errorlevel 1 ( exit /b 1 )
echo [3/4] ask...
amandac.exe ask --package build\exemplo.amanda "O que e entropia?" || exit /b 1
echo [4/4] serve (smoke 5s na porta 18080)...
start "" /min amandac.exe serve --package build\exemplo.amanda --port 18080 --conf-center 0.12 --conf-slope 12 --limiar-recusa 0.3
powershell -NoProfile -Command "Start-Sleep -Seconds 3"
powershell -NoProfile -Command "try { (Invoke-WebRequest -Uri 'http://127.0.0.1:18080/v1/models' -UseBasicParsing).Content } catch { Write-Host $_; exit 1 }" || (
  echo [ERRO] servidor nao respondeu
  taskkill /F /IM amandac.exe >nul 2>nul
  exit /b 1
)
powershell -NoProfile -Command "$b = @{ model='amanda'; messages=@(@{ role='user'; content='O que e entropia?' }) } | ConvertTo-Json -Depth 4; (Invoke-WebRequest -Uri 'http://127.0.0.1:18080/v1/chat/completions' -Method POST -Body $b -ContentType 'application/json' -UseBasicParsing).Content"
echo [4b/5] embeddings + sse...
curl.exe -s --max-time 10 -X POST http://127.0.0.1:18080/v1/embeddings -H "Content-Type: application/json" -d "@examples\smoke_embeddings.json" -o "%TEMP%\amanda_emb.json"
powershell -NoProfile -Command "$t = Get-Content $env:TEMP\amanda_emb.json -Raw; $n = ([regex]::Matches($t, '\d+\.\d+')).Count; if ($t.Contains('embedding') -and ($n -eq 384)) { Write-Host ('embeddings: OK floats=' + $n) } else { Write-Host ('embeddings: FALHA floats=' + $n); exit 1 }"
if errorlevel 1 ( taskkill /F /IM amandac.exe >nul 2>nul & exit /b 1 )
curl.exe -s --max-time 10 -N -X POST http://127.0.0.1:18080/v1/chat/completions -H "Content-Type: application/json" -d "@examples\smoke_stream.json" -o "%TEMP%\amanda_sse.txt"
powershell -NoProfile -Command "$t = Get-Content $env:TEMP\amanda_sse.txt -Raw; if (-not $t.Contains('data: [DONE]')) { Write-Host 'sse: SEM DONE'; exit 1 } else { Write-Host 'sse: OK' }"
if errorlevel 1 ( taskkill /F /IM amandac.exe >nul 2>nul & exit /b 1 )
echo [4c/5] eval endpoint...
curl.exe -s --max-time 20 -X POST http://127.0.0.1:18080/v1/eval -H "Content-Type: application/json" -d "@examples\smoke_eval.json" -o "%TEMP%\amanda_eval.json"
powershell -NoProfile -Command "$t = Get-Content $env:TEMP\amanda_eval.json -Raw; if (-not $t.Contains('cobertura')) { Write-Host 'eval-http: SEM cobertura'; exit 1 } else { Write-Host 'eval-http: OK' }"
if errorlevel 1 ( taskkill /F /IM amandac.exe >nul 2>nul & exit /b 1 )
taskkill /F /IM amandac.exe >nul 2>nul
echo [5/6] eval (Fase 5 - calibracao)...
amandac.exe eval --package build\exemplo.amanda --sample 1.0 || exit /b 1
echo [6/8] calibrate (Fase 6)...
amandac.exe calibrate --package build\exemplo.amanda --sample 1.0 || exit /b 1
echo [7/8] laya (engine externo, opcional)...
call "%~dp0check_laya.bat" || exit /b 1
echo [8/8] laya-llm (Fase 3, backend opcional)...
call "%~dp0check_laya_llm.bat" || exit /b 1
echo [9/10] mcp (MCP server via stdio)...
call "%~dp0check_mcp.bat" || exit /b 1
echo [10/10] docker (opcional, SKIP sem docker)...
call "%~dp0check_docker.bat" || exit /b 1
echo [OK] pipeline de integracao passou.
