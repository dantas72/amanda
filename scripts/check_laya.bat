@echo off
setlocal
REM ============================================================
REM  check_laya.bat — verifica o engine do Laya (v1.10.1+)
REM  Engine esperado em http://127.0.0.1:8420
REM  Saida: OK (engine no ar) ou SKIP (Laya nao instalado/rodando)
REM  Nunca falha a pipeline: retorna 0 nos dois casos.
REM  Retorna 1 apenas em erro real de script.
REM ============================================================
set LAYA_HOST=127.0.0.1
set LAYA_PORT=8420
if not "%LAYA_PORT_OVERRIDE%"=="" set LAYA_PORT=%LAYA_PORT_OVERRIDE%

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$base = 'http://%LAYA_HOST%:%LAYA_PORT%';" ^
  "$paths = @('/health', '/api/health', '/docs', '/');" ^
  "$ok = $false; $hit = '';" ^
  "foreach ($p in $paths) {" ^
  "  try {" ^
  "    $r = Invoke-WebRequest -Uri ($base + $p) -UseBasicParsing -TimeoutSec 5;" ^
  "    if ($r.StatusCode -ge 200 -and $r.StatusCode -lt 500) { $ok = $true; $hit = $p; break; }" ^
  "  } catch { }" ^
  "}" ^
  "if ($ok) { Write-Host ('[laya] OK: engine responde em ' + $base + $hit); exit 0 }" ^
  "else { Write-Host ('[laya] SKIP: engine do Laya nao responde em ' + $base + ' (instale o Laya e rode o engine para ativar esta etapa)'); exit 0 }"
exit /b 0
