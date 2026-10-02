# macOS — kit futuro (não implementado; só numa versão futura)

> Decisão do usuário (2026-10-02): **Mac só numa versão futura** (ele
> avisa quando). Nada de macOS no CI por enquanto — só Windows + Linux.
> Este arquivo deixa tudo pronto para aquele dia: basta um Mac real.

## Estado

- Tentativa Fase 14 (2026-10-02, revertida): matriz com `macos-15`
  (M1 ARM64) + `macos-15-intel` (x86_64). Build OK nas 2 archs, mas os
  **unit tests morrem em ~0s em ambas** (run 37075569182, etapa
  `Unit tests`). Crash na partida, igual em ARM e Intel — **não** é
  bug ARM. Sem o log da etapa, não dá para cravar a causa.
- Auditoria de portabilidade (sem Mac, só leitura): sem intrínsecos
  x86/SSE, little-endian byte-a-byte (`read/write_u32/i32/f32_le`),
  `SIGPIPE` via `signal(SIG_IGN)` (portátil), stack das threads do
  `serve` (~70KB: `char buf[65536]` + frames) dentro dos 512KB das
  threads secundárias do macOS, `usleep`/`gettimeofday`/`gmtime_r`
  disponíveis, Apple Clang compila C11 com `-Wall -Wextra` limpo
  (build passou no CI). Nada suspeito encontrado — o diagnóstico
  exige rodar num Mac real.

## Runners: o que existe (verificado 2026-10-02)

- Hospedados gratuitos: **só M1** (`macos-14/15/26`, `macos-latest`)
  e **Intel** (`macos-15-intel`, `macos-26-intel`).
- **M2 Pro**: só em *larger runners* pagos (labels `-xlarge` exigem
  runner criado na org — não resolvem num repo comum).
- **M3/M4**: não existem como labels — só via **self-hosted**
  (máquina própria com o agente do GitHub Actions).

## Como testar num Mac real

Pré-requisito: Xcode Command Line Tools (`xcode-select --install`,
traz Apple Clang + `cmake` via `brew install cmake` se preciso).

```sh
cmake -S . -B build && cmake --build build
ctest --test-dir build --output-on-failure   # igual ao CI
# ou o binário direto, com saída completa:
./build/amanda_tests
sh scripts/test_pipeline.sh
```

Para diagnosticar o crash do run 37075569182: rodar
`./build/amanda_tests` e anotar **onde para** (última suite impressa)
+ sinal de morte (`ctest` informa; ou `lldb -- ./build/amanda_tests`).

## Self-hosted (quando houver Mac M1–M4 disponível)

1. No repo: Settings → Actions → Runners → New self-hosted runner →
   macOS, seguir o script (`./config.sh`, `./run.sh` ou serviço).
2. Labels sugeridas: `self-hosted`, `macos`, `arm64` (+ `m4`, etc.).
3. Apontar o job: trocar `runs-on: ${{ matrix.os }}` por matriz que
   inclua `[self-hosted, macos, arm64]` (job separado ou matriz).
4. As etapas são as mesmas de `.github/workflows/ci.yml`
   (cmake + ctest + smoke bash) — portáteis por construção.

## Reativação (checklist do dia)

1. [ ] Rodar local num Mac (`amanda_tests` + pipeline) até 100%.
2. [ ] Se verde: recolocar `macos-15` (+ `macos-15-intel`) na matriz.
3. [ ] Atualizar `FASES.md` (Fase 14 → `[x]`), `README.md` (CI) e este arquivo.
