# Amanda — Compilador de Conhecimento em C

![Amanda](amandaLogo.png)

Repositório: `https://github.com/Spoiledpay/amanda`

Transforma `PDF / TXT / CSV / JSON` em artefato `.amanda` servido
localmente com API compatível OpenAI — pronto para ser consumido
por CLIs, OpenCode ou pelo Laya (ver `docs/laya.md`).

## Build (Windows)
```bat
build.bat
```
Gera `amandac.exe` e incrementa `version.bin` (começa em `1.0.1`).

## Build (Linux/Mac)
```sh
cmake -S . -B build && cmake --build build
```

## Uso
```sh
amandac compile --input examples/exemplo.txt --output exemplo.amanda
amandac inspect --package exemplo.amanda --stats
amandac ask --package exemplo.amanda "O que é entropia?"
amandac serve --package exemplo.amanda --port 8080
amandac version
```

## Testes
```bat
gcc -O2 -Iinclude tests\test_all.c src\amanda.c src\utils.c src\pdf_extractor.c src\chunker.c src\embedder.c src\question_gen.c src\decision_engine.c src\packager.c -o build\amanda_tests.exe && build\amanda_tests.exe
scripts\test_pipeline.bat
```

## Estrutura
- `include/` headers públicos
- `src/` implementação C11 sem dependências externas (só `ws2_32` no Windows)
- `docs/` especificações de formato, pipeline e API
- `tests/` testes unitários em C
- `scripts/` pipelines de integração
- `version.bin` versão lida pelo binário e incrementada a cada `build.bat`
