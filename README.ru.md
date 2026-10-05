# Amanda — Компилятор знаний для бизнеса

[![CI](https://github.com/dantas72/amanda/actions/workflows/ci.yml/badge.svg)](https://github.com/dantas72/amanda/actions)

![Amanda](amandaLogo.png)

Репозиторий: `https://github.com/dantas72/amanda`

Читать на: [Português](README.md) · [English](README.en.md) · [中文](README.zh.md)

© 2026 LabsObjects — создан и реализован Фернандо Дантасом, Бразилия.
Лицензия MIT: `LICENSE` (PT-BR), `LICENSE.en` (US English),
`LICENSE.ru` (RU), `LICENSE.zh` (CN).

Превращает документы вашей компании (`PDF / TXT / CSV / JSON`)
в артефакты решений `.amanda`: каждый ответ содержит указанный
источник и страницу, калиброванный уровень уверенности и
автоматический отказ отвечать вне области компетенции. Работает
на 100% локально без зависимости от облака и интегрируется с
вашими системами через CLI или API, совместимый с OpenAI, —
автономно или с генерацией через LLM (см. `docs/laya.md`). Идеально
для комплаенса, юриспруденции, финансов и поддержки клиентов.

## Сборка (Windows)
```bat
build.bat
```
Создаёт `amandac.exe` и увеличивает `version.bin` (текущая версия — в `## Версия` ниже).

## Сборка (Linux/Mac)
```sh
cmake -S . -B build && cmake --build build
cmake --build build --target version_bump   # паритет с build.bat (увеличивает version.bin)
```

## CI
GitHub Actions (`.github/workflows/ci.yml`): сборка + модульные тесты
(`ctest`) + интеграция (`compile`/`inspect`/`ask`/serve smoke) на
Windows и Linux (macOS на паузе — мгновенное падение модульных
тестов на M1 и Intel, см. `FASES.md`; у M2/M3/M4 нет бесплатных
хостед-меток), с артефактами `amandac` + `amanda_tests` для каждого
CI-релиза (фаза 7.6). Шаг Laya необязателен (SKIP без движка).

## Использование
```sh
amandac compile --input examples/exemplo.txt --output exemplo.amanda
amandac compile --config examples/config.yaml --output exemplo.amanda
amandac inspect --package exemplo.amanda --stats
amandac inspect --package exemplo.amanda --json   # машинный отчёт (фаза 7.6, с калибровкой v3)
amandac ask --package exemplo.amanda "O que é entropia?"
amandac ask --package exemplo.amanda "O que é entropia?" --json
amandac eval --package exemplo.amanda --sample 0.1
amandac calibrate --package exemplo.amanda --sample 0.5 --apply
amandac serve --package exemplo.amanda --port 8080
amandac serve --package cvm=livro1.amanda --package ibri=livro2.amanda --port 8080  # multi (фаза 13)
amandac mcp --package exemplo.amanda   # MCP-сервер через stdio (OpenCode/агенты, см. docs/mcp.md)
amandac version
```

## Метрики (фаза 5)

```sh
amandac eval --package exemplo.amanda --sample 0.1
amandac eval --package exemplo.amanda --sample 1.0 --json
```

Проверяет выборку типизированных вопросов и сообщает точность
(% верных страниц), калибровку (уверенность × точность, gap + ECE),
задержку (цель ≤500 мс) и отказы (в области + зонды вне области).
Подробности в `docs/eval.md`. Регрессия естественных ответов:
`scripts/check_gold.bat` (80 курированных вопросов, recall@2).

## Бэкенд Laya (фаза 3, необязательно)

```sh
amandac ask --package exemplo.amanda "O que é entropia?" --backend laya-http --laya-url http://127.0.0.1:8420
```

Отправляет (контекст + вопрос) в `POST /chat` движка Laya (LLM через
Ollama, напр. `nimble`) и сочетает с локальным граундингом
(страница/цитата/уверенность). Любой сбой автоматически переключается
на локальный движок. Подробности в `docs/laya.md`.

## Реальные бэкенды: JEV/TypeSafe + DeepSeek

```sh
amandac ask --package exemplo.amanda "O que é entropia?" --json --backend typesafe-http --typesafe-url http://127.0.0.1:11434 --typesafe-model nimble
TYPESAFE_API_KEY=... amandac ask --package exemplo.amanda "Pergunta" --backend typesafe-http --typesafe-model jev-latest
DEEPSEEK_API_KEY=... amandac ask --package exemplo.amanda "Pergunta" --backend deepseek-http
```

`typesafe-http` (алиас `jev`): настоящее noul-суждение — облако
`api.typesafe.ai` или локальный nimble в Ollama — калибрует
уверенность поверх локального граундинга; `deepseek-http`:
облачная перегенерация через DeepSeek. Ключи через флаг/env/файл
(никогда не логируются, никогда в `amanda.json`); endpoints и модели
в `--config-json` (`examples/amanda.json`). Честный локальный
фолбэк (поле `"backend"`). Реальный конвейер:
`scripts/check_typesafe.bat` + `scripts/check_deepseek.bat`
(SKIP без Ollama/ключей). Подробности в `docs/typesafe.md`,
`docs/deepseek.md` и `docs/jev.md`.

## Перекалибровка (фаза 6, хранится в v3 с 12.4)

```sh
amandac calibrate --package exemplo.amanda --sample 0.5
amandac calibrate --package exemplo.amanda --sample 0.5 --validacao examples/gold_cvm.json --apply
amandac ask --package exemplo.amanda "Pergunta" --conf-center 0.200 --conf-slope 16.0 --limiar-recusa 0.85
amandac eval --package exemplo.amanda --conf-center 0.200 --conf-slope 16.0 --limiar-recusa 0.85
```

`calibrate` ищет по сетке (центр × наклон × порог, шаг порога 0.01)
точку, максимизирующую сбалансированную точность (принимать в
области, отклонять зонды); с `--validacao` цель становится
`(bal + recall@2)/2` на размеченных естественных вопросах. `--apply`
записывает в пакет (формат `.amanda` v3); `ask`/`eval`/`serve`
подхватывают автоматически (флаг CLI главнее, `--ignore-calib`
принуждает стандарт). Нули = исторический стандарт. Подробности и
таблица по книгам в `docs/eval.md`.

## Serve (фазы 7.2/7.5/11/13 + пул LLM)

```sh
amandac serve --package exemplo.amanda --port 8080 --conf-center 0.200 --conf-slope 16.0 --limiar-recusa 0.85
amandac serve --config examples/config.yaml
AMANDA_API_KEY=segredo amandac serve --package exemplo.amanda --port 8080 --workers 8
```

Фиксированный пул воркеров с очередью (`--workers`, `--max-conns` =
общий лимит с `503`), ключ через флаг/env/файл (никогда не логируется),
`serve --config` (секция `servidor:`), лог доступа в `stderr`, без
собственного TLS (в проде за reverse-proxy). Мультипакет по `"model"`
(см. `docs/api.md`). С `--backend laya-http` chat/decisions идут в
движок Laya с автоматическим локальным фолбэком (поле `"backend"`):
пул LLM с приоритетом (`decisions` ВЫСОКИЙ > `chat` ОБЫЧНЫЙ,
`--laya-queue`/`--laya-queue-ms`; полная очередь или истёкшее ожидание =
локально, см. `docs/laya.md`).

## MCP-сервер (ask/decisions для OpenCode и агентов)

```sh
amandac mcp --package exemplo.amanda
```

MCP-сервер через stdio (JSON-RPC, по сообщению на строку, лог в
`stderr`): инструменты `ask`, `decisions`, `inspect`, `version`;
мультипакет по `model`, как в `serve`. Шаблон в
`examples/mcp_config.json`, подробности в `docs/mcp.md`.

## Docker

```sh
docker build -t amandac .
docker run --rm -p 8080:8080 -v /seus/amanda:/data amandac
```

Многостадийный образ (модульные тесты идут во время сборки);
entrypoint отдаёт все `/data/*.amanda` либо выполняет любую команду
(`mcp`, `inspect`, ...). Переменные (`PORT`, `AMANDA_API_KEY`,
`WORKERS`, калибровка...) в `docs/docker.md`.

## Статус фаз

Фазы 1–13 готовы (`FASES.md`, на португальском) + **MCP-сервер**
(`amandac mcp`: ask/decisions/inspect/version через stdio,
`docs/mcp.md`) + **Docker** (многостадийный образ с тестами в сборке,
`docs/docker.md`) + **пул LLM** с приоритетом (`decisions` > `chat`,
`docs/laya.md`): чистое C-ядро под Windows, CMake+CI, Laya по HTTP,
SSE+embeddings, `eval`, `calibrate` (+`--apply` v3 и `--validacao`),
извлечение PDF+, надёжный корпоративный serve (пул, мультипакет по
`model`), релизные артефакты, поиск BM25 + PT-стоп-слова +
мультицитирование, реранк (стемминг/мусор/насыщенная норма/фразы),
инвертированный индекс + кэш запросов. Ориентир (`amandac 1.0.39`,
пакеты v3): CVM 86.64%, IBRI 87.41%, INVEST 88.27%, Direito 84.00%;
задержка 0.3–5 мс; зонды 3/3; gold 75/80 проверен (см. `docs/eval.md`).

## Тесты
```bat
gcc -O2 -Wall -Wextra -std=c11 -Iinclude tests\test_all.c src\amanda.c src\utils.c src\pdf_extractor.c src\chunker.c src\embedder.c src\question_gen.c src\decision_engine.c src\laya_backend.c src\typesafe_backend.c src\deepseek_backend.c src\packager.c src\server.c src\eval.c src\calibra.c src\config.c src\mcp.c -o build\amanda_tests.exe -lws2_32 && build\amanda_tests.exe
scripts\test_pipeline.bat
scripts\check_gold.bat
```
Ориентир: **252 проверки** + конвейер (12 шагов, вкл. MCP smoke
и реальные бэкенды с честным SKIP)
+ gold (80 вопросов, recall@2). CI: Windows + Linux (`ctest` + MCP
smoke + сборка Docker); macOS на паузе (см. `FASES.md`).

## Структура
- `include/` публичные заголовки
- `src/` реализация на C11 без внешних зависимостей (только `ws2_32` на Windows)
- `docs/` формат (`formato_amanda.md`), конвейер, API, eval, Laya, руководство, бизнес-туториал, MCP (`mcp.md`), Docker (`docker.md`)
- `templates/` формулировки типизированных вопросов (+ паки `empresas/`: compliance, финансы, юриспруденция, поддержка)
- `examples/` пример, `config.yaml`, smoke-фикстуры и регрессионные gold-наборы
- `tests/` модульные тесты на C
- `scripts/` интеграционные конвейеры
- `version.bin` версия, читаемая бинарником и увеличиваемая при каждом `build.bat`

## Дорожная карта (задокументировано; не реализовано)
- **Тесты на GPU**: повторить живой Laya (nimble + llama3.2:3B) на GTX 1660 Ti и GPU 10GB+ (см. `docs/laya.md`).
- **CI macOS + self-hosted M2-M4**: реактивировать с логом падения
  (run 37075569182, шаг Unit tests, 0s на M1 и Intel) или локальным
  тестом на настоящем Mac; кит готов в `docs/macos.md`.

## Версия
`version.bin` — источник истины, увеличивается при каждом `build.bat`.
Последняя локальная сборка:
Build: `1.0.45`
