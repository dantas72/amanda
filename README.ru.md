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
Windows и Linux (macOS временно исключён; см. фазу 7.5), с
артефактами `amandac` + `amanda_tests` для каждого CI-релиза (фаза 7.6).
Шаг Laya необязателен (SKIP без движка).

## Использование
```sh
amandac compile --input examples/exemplo.txt --output exemplo.amanda
amandac compile --config examples/config.yaml --output exemplo.amanda
amandac inspect --package exemplo.amanda --stats
amandac inspect --package exemplo.amanda --json   # машинный отчёт (фаза 7.6)
amandac ask --package exemplo.amanda "O que é entropia?"
amandac eval --package exemplo.amanda --sample 0.1
amandac serve --package exemplo.amanda --port 8080
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
Подробности в `docs/eval.md`.

## Бэкенд Laya (фаза 3, необязательно)

```sh
amandac ask --package exemplo.amanda "O que é entropia?" --backend laya-http --laya-url http://127.0.0.1:8420
```

Отправляет (контекст + вопрос) в `POST /chat` движка Laya (LLM через
Ollama, напр. `nimble`) и сочетает с локальным граундингом
(страница/цитата/уверенность). Любой сбой автоматически переключается
на локальный движок. Подробности в `docs/laya.md`.

## Перекалибровка (фаза 6)

```sh
amandac calibrate --package exemplo.amanda --sample 0.5
amandac ask --package exemplo.amanda "Pergunta" --conf-center 0.100 --conf-slope 22.0 --limiar-recusa 0.90
amandac eval --package exemplo.amanda --conf-center 0.100 --conf-slope 22.0 --limiar-recusa 0.90
```

`calibrate` предлагает центр/наклон/порог, максимизирующие
сбалансированную точность (принимать в области, отклонять зонды).
Нули = исторический стандарт. Подробности и таблица по книгам в
`docs/eval.md`.

## Калиброванный serve (фаза 7.2)

```sh
amandac serve --package exemplo.amanda --port 8080 --conf-center 0.100 --conf-slope 22.0 --limiar-recusa 0.90
```

Применяет калибровку `calibrate`/`eval` к серверу (баннер показывает
активные параметры). Бэкенд serve всегда локальный: при
однопоточном сервере один LLM-вывод на запрос останавливал бы сервис
(пересмотреть в фазе 7.5, с потоками). См. `docs/api.md`.

## Статус фаз

Фазы 1–6 + 7.1 готовы (`FASES.md`, на португальском): ядро, CMake+CI,
Laya-HTTP, SSE+embeddings, `eval`, `calibrate`, доки/гигиена.
В работе: фаза 7 (7.2 калиброванный serve, 7.3 config+templates,
7.4 PDF+, 7.5 надёжный сервер, 7.6 релиз).

## Тесты
```bat
gcc -O2 -Iinclude tests\test_all.c src\amanda.c src\utils.c src\pdf_extractor.c src\chunker.c src\embedder.c src\question_gen.c src\decision_engine.c src\laya_backend.c src\packager.c src\eval.c src\calibra.c src\config.c -o build\amanda_tests.exe -lws2_32 && build\amanda_tests.exe
scripts\test_pipeline.bat
```

## Структура
- `include/` публичные заголовки
- `src/` реализация на C11 без внешних зависимостей (только `ws2_32` на Windows)
- `docs/` спецификации формата, конвейера и API
- `tests/` модульные тесты на C
- `scripts/` интеграционные конвейеры
- `version.bin` версия, читаемая бинарником и увеличиваемая при каждом `build.bat`

## Версия
`version.bin` — источник истины, увеличивается при каждом `build.bat`.
Последняя локальная сборка:
Build: `1.0.31`
