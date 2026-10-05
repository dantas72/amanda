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
覆盖 Windows 与 Linux（macOS 已暂停——M1 与 Intel 上单元测试瞬间
崩溃，见 `FASES.md`；M2/M3/M4 没有免费托管标签），每次 CI
发布附带 `amandac` + `amanda_tests` 产物（第 7.6 阶段）。
Laya 步骤为可选项（无引擎时 SKIP）。

## 用法
```sh
amandac compile --input examples/exemplo.txt --output exemplo.amanda
amandac compile --config examples/config.yaml --output exemplo.amanda
amandac inspect --package exemplo.amanda --stats
amandac inspect --package exemplo.amanda --json   # 机器报告（第 7.6 阶段，含 v3 校准）
amandac ask --package exemplo.amanda "O que é entropia?"
amandac ask --package exemplo.amanda "O que é entropia?" --json
amandac eval --package exemplo.amanda --sample 0.1
amandac calibrate --package exemplo.amanda --sample 0.5 --apply
amandac serve --package exemplo.amanda --port 8080
amandac serve --package cvm=livro1.amanda --package ibri=livro2.amanda --port 8080  # 多包（第 13 阶段）
amandac mcp --package exemplo.amanda   # 经 stdio 的 MCP 服务（OpenCode/智能体，见 docs/mcp.md）
amandac version
```

## 指标（第 5 阶段）

```sh
amandac eval --package exemplo.amanda --sample 0.1
amandac eval --package exemplo.amanda --sample 1.0 --json
```

抽查类型化问题并报告保真度（正确页码占比）、校准度（置信度 ×
准确率、gap + ECE）、延迟（目标 ≤500ms）与拒绝率（范围内 +
范围外探针）。详见 `docs/eval.md`。自然问答回归：
`scripts/check_gold.bat`（80 条精选问题，recall@2）。

## Laya 后端（第 3 阶段，可选）

```sh
amandac ask --package exemplo.amanda "O que é entropia?" --backend laya-http --laya-url http://127.0.0.1:8420
```

将（上下文 + 问题）发送至 Laya 引擎的 `POST /chat`（经 Ollama
调用 LLM，如 `nimble`），并与本地 grounding（页码/引用/置信度）
结合。任何失败都会自动回退到本地引擎。详见 `docs/laya.md`。

## 真实后端：JEV/TypeSafe + DeepSeek

```sh
amandac ask --package exemplo.amanda "O que é entropia?" --json --backend typesafe-http --typesafe-url http://127.0.0.1:11434 --typesafe-model nimble
TYPESAFE_API_KEY=... amandac ask --package exemplo.amanda "Pergunta" --backend typesafe-http --typesafe-model jev-latest
DEEPSEEK_API_KEY=... amandac ask --package exemplo.amanda "Pergunta" --backend deepseek-http
```

`typesafe-http`（别名 `jev`）：真实 noul 裁决——TypeSafe 云
`api.typesafe.ai` 或本地 Ollama nimble——在本地 grounding 之上校准
置信度；`deepseek-http`：经 DeepSeek 云重写。密钥经标志/环境/文件
（永不记入日志，永不写入 `amanda.json`）；端点与模型在
`--config-json`（`examples/amanda.json`）。诚实的本地回退
（`"backend"` 字段）。真实流水线：`scripts/check_typesafe.bat` +
`scripts/check_deepseek.bat`（无 Ollama/密钥时 SKIP）。详见
`docs/typesafe.md`、`docs/deepseek.md` 与 `docs/jev.md`。

## 重校准（第 6 阶段，12.4 起存入 v3）

```sh
amandac calibrate --package exemplo.amanda --sample 0.5
amandac calibrate --package exemplo.amanda --sample 0.5 --validacao examples/gold_cvm.json --apply
amandac ask --package exemplo.amanda "Pergunta" --conf-center 0.200 --conf-slope 16.0 --limiar-recusa 0.85
amandac eval --package exemplo.amanda --conf-center 0.200 --conf-slope 16.0 --limiar-recusa 0.85
```

`calibrate` 在网格（中心 × 斜率 × 阈值，阈值步长 0.01）中搜索使
平衡准确率最大（接受范围内、拒绝探针）的点；加 `--validacao` 后
目标变为标注自然问题上的 `(bal + recall@2)/2`。`--apply` 写入包
（`.amanda` v3 格式）；`ask`/`eval`/`serve` 自动采用（CLI 标志优先，
`--ignore-calib` 强制默认值）。零值 = 历史默认值。详见
`docs/eval.md`（含各书参数表）。

## serve（第 7.2/7.5/11/13 阶段 + LLM 池）

```sh
amandac serve --package exemplo.amanda --port 8080 --conf-center 0.200 --conf-slope 16.0 --limiar-recusa 0.85
amandac serve --config examples/config.yaml
AMANDA_API_KEY=segredo amandac serve --package exemplo.amanda --port 8080 --workers 8
```

固定 worker 池 + 队列（`--workers`，`--max-conns` 为 `503` 的总量
上限），密钥经标志/环境/文件（永不记入日志），`serve --config`
（`servidor:` 节），访问日志输出到 `stderr`，无内置 TLS（生产环境
置于反向代理之后）。按 `"model"` 路由多包（见 `docs/api.md`）。
`--backend laya-http` 下 chat/decisions 请求 Laya 引擎并自动本地
回退（`"backend"` 字段）：带优先级的 LLM 池（`decisions` 高 >
`chat` 普通，`--laya-queue`/`--laya-queue-ms`；队列满或等待超时
即回退本地，见 `docs/laya.md`）。

## MCP 服务（供 OpenCode 与智能体调用 ask/decisions）

```sh
amandac mcp --package exemplo.amanda
```

经 stdio 的 MCP 服务（JSON-RPC，每行一条消息，日志走 `stderr`）：
工具 `ask`、`decisions`、`inspect`、`version`；与 `serve` 一样按
`model` 路由多包。模板见 `examples/mcp_config.json`，详见
`docs/mcp.md`。

## Docker

```sh
docker build -t amandac .
docker run --rm -p 8080:8080 -v /seus/amanda:/data amandac
```

多阶段镜像（构建期运行单元测试）；entrypoint 服务全部
`/data/*.amanda`，或直接执行任意命令（`mcp`、`inspect`……）。
变量（`PORT`、`AMANDA_API_KEY`、`WORKERS`、校准……）见
`docs/docker.md`。

## 阶段状态

第 1–13 阶段已完成（`FASES.md`，葡萄牙语）+ **MCP 服务**
（`amandac mcp`：经 stdio 的 ask/decisions/inspect/version，
`docs/mcp.md`）+ **Docker**（构建期跑测试的多阶段镜像，
`docs/docker.md`）+ **带优先级的 LLM 池**（`decisions` >
`chat`，`docs/laya.md`）：纯 C Windows 核心、CMake+CI、经 HTTP
的 Laya、SSE+embeddings、`eval`、`calibrate`（+`--apply` v3 与
`--validacao`）、PDF+ 抽取、稳健的企业级 serve（池、按 `model`
多包）、发布产物、BM25 检索 + 葡语停用词 + 多引用、重排
（词干/垃圾过滤/饱和范数/短语）、倒排索引 + 查询缓存。基准
（`amandac 1.0.39`，v3 包）：CVM 86.64%、IBRI 87.41%、INVEST
88.27%、Direito 84.00%；延迟 0.3–5ms；探针 3/3；gold 75/80 已审计
（见 `docs/eval.md`）。

## 测试
```bat
gcc -O2 -Wall -Wextra -std=c11 -Iinclude tests\test_all.c src\amanda.c src\utils.c src\pdf_extractor.c src\chunker.c src\embedder.c src\question_gen.c src\decision_engine.c src\laya_backend.c src\typesafe_backend.c src\deepseek_backend.c src\packager.c src\server.c src\eval.c src\calibra.c src\config.c src\mcp.c -o build\amanda_tests.exe -lws2_32 && build\amanda_tests.exe
scripts\test_pipeline.bat
scripts\check_gold.bat
```
基准：**252 项检查** + 流水线（12 步，含 MCP smoke 与诚实 SKIP 的真实后端）+ gold（80
问，recall@2）。CI：Windows + Linux（`ctest` + MCP smoke +
Docker 构建）；macOS 已暂停（见 `FASES.md`）。

## 结构
- `include/` 公开头文件
- `src/` 无外部依赖的 C11 实现（Windows 上仅需 `ws2_32`）
- `docs/` 格式（`formato_amanda.md`）、流水线、API、eval、Laya、使用指南、企业教程、MCP（`mcp.md`）、Docker（`docker.md`）
- `templates/` 类型化问题措辞（+ `empresas/` 包：合规、金融、法务、客服）
- `examples/` 示例、`config.yaml`、smoke 夹具与回归 gold 集
- `tests/` C 语言单元测试
- `scripts/` 集成流水线
- `version.bin` 由二进制读取、每次 `build.bat` 递增的版本

## 未来路线（已记录；尚未实现）
- **GPU 测试**：在 GTX 1660 Ti 与 10GB+ 显存上重复 live Laya（nimble + llama3.2:3B，见 `docs/laya.md`）。
- **macOS CI + 自建 M2-M4**：凭崩溃日志重新启用（run 37075569182，
  Unit tests 步骤，M1 与 Intel 均为 0s）或在真机 Mac 上本地测试；
  工具包见 `docs/macos.md`。

## 版本
`version.bin` 是唯一的版本来源，每次 `build.bat` 递增。
最近一次本地构建：
Build: `1.0.45`
