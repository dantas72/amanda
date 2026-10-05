@echo off
setlocal
REM Valida o Dockerfile. SKIP honesto sem docker; falha de verdade com docker.
cd /d "%~dp0\.."
where docker >nul 2>nul
if errorlevel 1 (
  echo [docker] SKIP: docker nao instalado
  exit /b 0
)
docker build -t amandac-test .
if errorlevel 1 (
  echo [docker] FALHA: docker build quebrou
  exit /b 1
)
echo [docker] build: OK
docker run --rm amandac-test version
if errorlevel 1 (
  echo [docker] FALHA: entrypoint version quebrou
  exit /b 1
)
echo [OK] docker smoke passou.
