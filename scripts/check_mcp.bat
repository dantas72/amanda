@echo off
setlocal
REM Smoke do MCP server via stdio: initialize -> tools/list -> ask/decisions/version/ping.
REM Protocolo JSON-RPC por linha; notificacao nao responde. CRLF.
cd /d "%~dp0\.."
if not exist amandac.exe (
  echo [ERRO] amandac.exe nao encontrado. Rode build.bat primeiro.
  exit /b 1
)
if not exist build\exemplo.amanda (
  echo [mcp] compilando pacote de teste...
  amandac.exe compile --input examples\exemplo.txt --output build\exemplo.amanda --title "Exemplo Amanda"
  if errorlevel 1 exit /b 1
)
set REQ=%TEMP%\amanda_mcp_req.txt
set OUT=%TEMP%\amanda_mcp_out.txt
set ERR=%TEMP%\amanda_mcp_err.txt
echo {"jsonrpc":"2.0","id":1,"method":"initialize","params":{}} > "%REQ%"
echo {"jsonrpc":"2.0","method":"notifications/initialized"} >> "%REQ%"
echo {"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}} >> "%REQ%"
echo {"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"ask","arguments":{"pergunta":"O que e entropia?"}}} >> "%REQ%"
echo {"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"decisions","arguments":{"pergunta":"O que e entropia?"}}} >> "%REQ%"
echo {"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"version","arguments":{}}} >> "%REQ%"
echo {"jsonrpc":"2.0","id":"a-b","method":"ping"} >> "%REQ%"
echo {"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"inexistente","arguments":{}}} >> "%REQ%"
type "%REQ%" | amandac.exe mcp --package build\exemplo.amanda > "%OUT%" 2> "%ERR%"
if errorlevel 1 (
  echo [mcp] FALHA: processo mcp saiu com erro
  exit /b 1
)
findstr /c:"protocolVersion" "%OUT%" >nul
if errorlevel 1 (
  echo [mcp] FALHA: initialize sem protocolVersion
  exit /b 1
)
echo [mcp] initialize: OK
findstr /c:"ask" "%OUT%" >nul
if errorlevel 1 (
  echo [mcp] FALHA: tools/list sem ferramenta ask
  exit /b 1
)
findstr /c:"decisions" "%OUT%" >nul
if errorlevel 1 (
  echo [mcp] FALHA: tools/list sem ferramenta decisions
  exit /b 1
)
echo [mcp] tools/list: OK
findstr /c:"confianca" "%OUT%" >nul
if errorlevel 1 (
  echo [mcp] FALHA: ask sem confianca na resposta
  exit /b 1
)
echo [mcp] ask: OK
findstr /c:"probabilidade" "%OUT%" >nul
if errorlevel 1 (
  echo [mcp] FALHA: decisions sem probabilidade
  exit /b 1
)
echo [mcp] decisions: OK
findstr /c:"amandac" "%OUT%" >nul
if errorlevel 1 (
  echo [mcp] FALHA: version sem amandac
  exit /b 1
)
echo [mcp] version: OK
findstr /c:"a-b" "%OUT%" >nul
if errorlevel 1 (
  echo [mcp] FALHA: ping com id string sem eco verbatim
  exit /b 1
)
echo [mcp] ping: OK
findstr /c:"32602" "%OUT%" >nul
if errorlevel 1 (
  echo [mcp] FALHA: ferramenta inexistente sem erro -32602
  exit /b 1
)
echo [mcp] erro -32602: OK
echo [OK] mcp smoke passou.
