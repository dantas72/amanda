@echo off
setlocal EnableDelayedExpansion
REM ============================================================
REM  Amanda - pipeline de compilacao Windows (Fase 1)
REM  Gera amandac.exe + version.bin com incremento automatico
REM ============================================================

cd /d "%~dp0"

if not exist "version.bin" (
  echo 1.0.1> version.bin
  echo [build] version.bin criado: 1.0.1
) else (
  set /p VER=< version.bin
  REM remove espacos
  set VER=!VER: =!
  for /f "tokens=1,2,3 delims=." %%a in ("!VER!") do (
    set MAJ=%%a
    set MIN=%%b
    set PAT=%%c
  )
  if "!MAJ!"=="" set MAJ=1
  if "!MIN!"=="" set MIN=0
  if "!PAT!"=="" set PAT=1
  set /a PAT=!PAT!+1
  echo !MAJ!.!MIN!.!PAT!> version.bin
  echo [build] versao incrementada: !MAJ!.!MIN!.!PAT!
)

set /p AVER=< version.bin
set AVER=%AVER: =%

REM gera header de versao
(
  echo #ifndef AMANDA_VERSION_GEN_H
  echo #define AMANDA_VERSION_GEN_H
  echo #define AMANDA_VERSION_GEN "%AVER%"
  echo #endif
) > include\version_gen.h
echo [build] include\version_gen.h = %AVER%

where gcc >nul 2>nul
if errorlevel 1 (
  echo [ERRO] gcc nao encontrado no PATH. Instale MinGW-w64.
  exit /b 1
)

echo [build] compilando amandac.exe ...
gcc -O2 -Wall -Wextra -std=c11 -Iinclude src\amanda.c src\utils.c src\pdf_extractor.c src\chunker.c src\embedder.c src\question_gen.c src\decision_engine.c src\laya_backend.c src\packager.c src\server.c src\eval.c src\cli.c src\main.c -o amandac.exe -lws2_32
if errorlevel 1 (
  echo [ERRO] falha na compilacao.
  exit /b 1
)

echo [build] OK: amandac.exe (%AVER%)
amandac.exe version
exit /b 0
