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
echo [2/4] inspect...
amandac.exe inspect --package build\exemplo.amanda --stats || exit /b 1
echo [3/4] ask...
amandac.exe ask --package build\exemplo.amanda "O que e entropia?" || exit /b 1
echo [4/4] serve (smoke 5s na porta 18080)...
start "" /min amandac.exe serve --package build\exemplo.amanda --port 18080
powershell -NoProfile -Command "Start-Sleep -Seconds 3"
powershell -NoProfile -Command "try { (Invoke-WebRequest -Uri 'http://127.0.0.1:18080/v1/models' -UseBasicParsing).Content } catch { Write-Host $_; exit 1 }" || (
  echo [ERRO] servidor nao respondeu
  taskkill /F /IM amandac.exe >nul 2>nul
  exit /b 1
)
powershell -NoProfile -Command "$b = @{ model='amanda'; messages=@(@{ role='user'; content='O que e entropia?' }) } | ConvertTo-Json -Depth 4; (Invoke-WebRequest -Uri 'http://127.0.0.1:18080/v1/chat/completions' -Method POST -Body $b -ContentType 'application/json' -UseBasicParsing).Content"
taskkill /F /IM amandac.exe >nul 2>nul
echo [5/5] laya (engine externo, opcional)...
call "%~dp0check_laya.bat" || exit /b 1
echo [OK] pipeline de integracao passou.
