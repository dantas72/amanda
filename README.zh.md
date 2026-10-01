# Amanda — 企业知识编译器

[![CI](https://github.com/dantas72/amanda/actions/workflows/ci.yml/badge.svg)](https://github.com/dantas72/amanda/actions)

![Amanda](amandaLogo.png)

仓库：`https://github.com/dantas72/amanda`

阅读其他语言：[Português](README.md) · [English](README.en.md) · [Русский](README.ru.md)

© 2026 LabsObjects — 由 Fernando Dantas（巴西）创建与实现。
MIT 许可证：`LICENSE`（PT-BR）、`LICENSE.en`（US English）、
`LICENSE.ru`（RU）、`LICENSE.zh`（CN）。

将企业运营文档（`PDF / TXT / CSV / JSON`）编译为 `.amanda`
决策产物：每条回答都附带引用的来源与页码、经过校准的置信度，
并对范围外问题自动拒绝。100% 本地运行，不依赖云端，通过 CLI
或兼容 OpenAI 的 API 与业务系统集成——可独立运行，也可经 LLM
起草（见 `docs/laya.md`）。适用于合规、法务、财务与客服场景。

## 构建（Windows）
```bat
build.bat
```
生成 `amandac.exe` 并递增 `version.bin`（当前版本见下文 `## 版本`）。

## 构建（Linux/Mac）
```sh
cmake -S . -B build && cmake --build build
cmake --build build --target version_bump   # 与 build.bat 对等（递增 version.bin）
```

## CI
GitHub Actions（`.github/workflows/ci.yml`）：构建 + 单元测试
（`ctest`）+ 集成测试（`compile`/`inspect`/`ask`/serve smoke），
覆盖 Windows 与 Linux（macOS 暂时除外，见第 7.5 阶段），每次 CI
发布附带 `amandac` + `amanda_tests` 产物（第 7.6 阶段）。
Laya 步骤为可选项（无引擎时 SKIP）。

## 用法
```sh
amandac compile --input examples/exemplo.txt --output exemplo.amanda
amandac compile --config examples/config.yaml --output exemplo.amanda
amandac inspect --package exemplo.amanda --stats
amandac inspect --package exemplo.amanda --json   # 机器报告（第 7.6 阶段）
amandac ask --package exemplo.amanda "O que é entropia?"
amandac eval --package exemplo.amanda --sample 0.1
amandac serve --package exemplo.amanda --port 8080
amandac version
```

## 指标（第 5 阶段）

```sh
amandac eval --package exemplo.amanda --sample 0.1
amandac eval --package exemplo.amanda --sample 1.0 --json
```

抽查类型化问题并报告保真度（正确页码占比）、校准度（置信度 ×
准确率、gap + ECE）、延迟（目标 ≤500ms）与拒绝率（范围内 +
范围外探针）。详见 `docs/eval.md`。

## Laya 后端（第 3 阶段，可选）

```sh
amandac ask --package exemplo.amanda "O que é entropia?" --backend laya-http --laya-url http://127.0.0.1:8420
```

将（上下文 + 问题）发送至 Laya 引擎的 `POST /chat`（经 Ollama
调用 LLM，如 `nimble`），并与本地 grounding（页码/引用/置信度）
结合。任何失败都会自动回退到本地引擎。详见 `docs/laya.md`。

## 重校准（第 6 阶段）

```sh
amandac calibrate --package exemplo.amanda --sample 0.5
amandac ask --package exemplo.amanda "Pergunta" --conf-center 0.100 --conf-slope 22.0 --limiar-recusa 0.90
amandac eval --package exemplo.amanda --conf-center 0.100 --conf-slope 22.0 --limiar-recusa 0.90
```

`calibrate` 会给出使平衡准确率最大（接受范围内、拒绝探针）的
中心/斜率/阈值。零值 = 历史默认值。详见 `docs/eval.md`（含各书参数表）。

## 校准后的 serve（第 7.2 阶段）

```sh
amandac serve --package exemplo.amanda --port 8080 --conf-center 0.100 --conf-slope 22.0 --limiar-recusa 0.90
```

将 `calibrate`/`eval` 的校准应用于服务器（banner 显示生效参数）。
serve 后端始终为本地：单线程服务器下每个请求一次 LLM 推理会阻塞
服务（第 7.5 阶段以多线程重审）。见 `docs/api.md`。

## 阶段状态

第 1–6 + 7.1 阶段已完成（`FASES.md`，葡萄牙语）：核心、CMake+CI、
Laya-HTTP、SSE+embeddings、`eval`、`calibrate`、文档/清理。进行中：
第 7 阶段（7.2 校准 serve、7.3 config+templates、7.4 PDF+、7.5 稳健
服务器、7.6 发布）。

## 测试
```bat
gcc -O2 -Iinclude tests\test_all.c src\amanda.c src\utils.c src\pdf_extractor.c src\chunker.c src\embedder.c src\question_gen.c src\decision_engine.c src\laya_backend.c src\packager.c src\eval.c src\calibra.c src\config.c -o build\amanda_tests.exe -lws2_32 && build\amanda_tests.exe
scripts\test_pipeline.bat
```

## 结构
- `include/` 公开头文件
- `src/` 无外部依赖的 C11 实现（Windows 上仅需 `ws2_32`）
- `docs/` 格式、流水线与 API 规范
- `tests/` C 语言单元测试
- `scripts/` 集成流水线
- `version.bin` 由二进制读取、每次 `build.bat` 递增的版本

## 版本
`version.bin` 是唯一的版本来源，每次 `build.bat` 递增。
最近一次本地构建：
Build: `1.0.25`
