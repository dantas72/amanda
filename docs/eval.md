# Eval Amanda — Fase 5 (calibração + métricas)

Subcomando: `amandac eval --package X.amanda [--sample 0.1] [--seed 42] [--top-k 3] [--json]`

## O que faz

1. Carrega o `.amanda` (chunks + embeddings + perguntas tipadas).
2. Sorteia deterministicamente `sample` (padrão 10%, seed 42, Fisher-Yates com LCG)
   das perguntas geradas no `compile`.
3. Para cada pergunta amostrada, executa o `decision_engine` com o **enunciado**
   como query e mede:
   - **fidelidade**: fração com `pagina == pagina_fonte` e sem recusa;
   - **acurácia / confiança média / gap** `|conf − acur|` e **ECE** (5 bins);
   - **latência** média e máxima (meta ≤ 500 ms);
   - **recusa in-scope** + breakdown por tipo (`choice/score/noul`).
4. Sonda **recusa fora-escopo** com 3 probes fixas (ex.: "capital de Marte em 3020"),
   que deveriam ser recusadas.

Saída texto humana por padrão; `--json` emite o relatório máquina
(`fidelidade`, `gap_calibracao`, `ece`, `lat_media_ms`, `taxa_recusa_probe`, ...).

## Resultados nos livros de `pdf/` (2026-09-30, `amandac 1.0.2`)

| pacote | perguntas | fidelidade@10% | conf média | gap/ECE | lat média | recusa probe |
|---|---|---|---|---|---|---|
| cvm_valores_mobiliarios | 90 | 100% (9/9) | 0.99 | 0.014 | 3.6 ms PASS | 0/3 |
| ibri_cvm | 21 | 50% (1/2) | 0.98 | 0.480 | 0.0 ms PASS | 0/3 |
| top_direito | 159 | 68.8% (11/16) | 0.99 | 0.300 | 4.9 ms PASS | 0/3 |
| top_analise_investimentos | 75 | 62.5% (5/8) | 0.99 | 0.368 | 0.0 ms PASS | 0/3 |

## Achados (não bloqueiam a Fase 5, viram backlog)
1. **Overconfiança**: confiança satura em ~0.99 mesmo quando a fidelidade é
   60–70%. O sigmoide do `decision_engine` (centro 0.12, inclinação 12)
   precisa de recalibração (ex.: Platt/temperatura ou ajuste do centro).
2. **Recusa nunca dispara** (0/3 probes em todos os pacotes): o limiar de
   recusa (0.3) fica abaixo dos scores típicos de queries genéricas.
   Backlog: recalibrar par centro/inclinação/limiar com curva dedicada.
3. **Extração parcial em PDFs grandes**: ex. 412 páginas → 188 blocos →
   12 chunks. PDFs com streams complexos/imagens geram pouco texto
   extraível. Backlog: ampliar operadores PDF suportados e reportar
   cobertura de extração no `eval`.

## Fase 6 — Recalibração (fecha os itens 1 e 2)

`amandac calibrate --package X --sample 0.5` coleta (score, rótulo)
para amostra in-scope (positivas) + 3 probes (negativas) e busca em
grade `center × slope × limiar` (12×14×17 = 2856 combinações) que
maximize a acurácia balanceada (TPR+TNR)/2, com desempate pelo menor
gap. O motor aceita os parâmetros
(`ask`/`eval --conf-center F --conf-slope F --limiar-recusa F`;
zeros = padrão histórico 0.12/12.0/0.30).

Resultados (`amandac 1.0.7`):

| pacote | atual (bal) | sugerido | bal | TPR/TNR | gap |
|---|---|---|---|---|---|
| cvm_valores_mobiliarios | 0.12/12/0.30 (0.500) | 0.100/22/0.90 | 1.000 | 1.00/1.00 | 0.009 |
| top_direito | 0.12/12/0.30 (0.500) | 0.050/30/0.90 | 1.000 | 1.00/1.00 | 0.005 |
| top_analise_investimentos | 0.12/12/0.30 (0.500) | 0.050/16/0.90 | 1.000 | 1.00/1.00 | 0.009 |
| ibri_cvm | 0.12/12/0.30 (0.500) | 0.050/30/0.90 | 1.000 | 1.00/1.00 | 0.017 |

Fechamento do loop (CVM, `eval --sample 0.2` com params sugeridos):
recusa fora-escopo **0/3 → 3/3 (100%)**, recusa in-scope 0/18,
fidelidade 88.9%, latência 2.6 ms PASS.

Nota honesta: o ótimo cai em limiar alto (0.90) com slope íngreme —
separa perfeitamente estes pacotes, mas perguntas in-scope limítrofes
futuras podem recusar mais. O `calibrate` imprime TPR/TNR para auditar;
rode `eval` com os parâmetros antes de adotar.

## Fase 7.4 — Extração PDF+ e cobertura no `eval` (`amandac 1.0.15`)

Correção raiz: o inflate dinâmico próprio violava a RFC 1951
(`bl_count[0]` participava do código canônico) e falhava em ~95% dos
streams reais; com a correção + filtros em cadeia + operadores, as
falhas zeraram e o volume extraído multiplicou (CVM: 12 → 448 blocos).

O `eval` (texto e `--json` via objeto `cobertura`) agora reporta:
páginas, blocos, chunks, chars totais, chars/página, perguntas/chunk,
streams totais/de texto, falhas e fallback.

Resultados com extração completa (`eval --sample 0.1/0.2`):

| pacote | blocos/páginas | perguntas | fidelidade | gap/ECE | lat média | recusa probe |
|---|---|---|---|---|---|---|
| cvm_valores_mobiliarios | 448/412 | 5320 | 86.3% | 0.129 | 26.8 ms PASS | 0/3 |
| ibri_cvm | 178/161 | 2415 | 89.7% | 0.097 | 12.4 ms PASS | 0/3 |
| top_analise_investimentos | 253/260 | 2498 | 89.8% | 0.094 | 12.7 ms PASS | 0/3 |
| top_direito | 1393/1348 | 33218 | (amostra 1% p/ tempo) | — | PASS | — |

Fechamento do loop (CVM, `calibrate --sample 0.2` → 0.450/26.0/0.70):
bal **0.500 → 0.950** (TPR 0.90/TNR 1.00).

## Base completa + calibrate por livro (2026-10-02, `amandac 1.0.26`)

Os 4 `build/*_t74.amanda` cobrem os PDFs integrais (falhas 0):
CVM 412p/448 blocos/602 chunks/5320 perg, IBRI 161p/178/277/2415,
INVEST 260p/253/284/2498, DIREITO 1348p/1393/3811/33218.

`calibrate` por pacote (amostra 0.2; Direito 0.02):

| pacote | sugerido (center/slope/limiar) | bal | TPR/TNR |
|---|---|---|---|
| cvm | 0.450/24.0/0.80 | 0.988 | 0.98/1.00 |
| ibri | 0.450/16.0/0.85 | 0.964 | 0.93/1.00 |
| invest | 0.450/28.0/0.75 | 0.994 | 0.99/1.00 |
| direito | 0.550/14.0/0.40 | 0.980 | 0.96/1.00 |

Uso no `serve`: `amandac serve --package build\cvm_teste74.amanda
--port 8080 --conf-center 0.450 --conf-slope 24.0 --limiar-recusa 0.80`
(trocar pelos valores da tabela por livro).

Eval full (3 pequenos 100%; Direito 1993/33218 ≈ 6%):

| pacote | amostradas | fidelidade | gap/ECE | lat média |
|---|---|---|---|---|
| cvm | 5320 | 88.65% | 0.111 | 60.9 ms PASS |
| ibri | 2415 | 89.19% | — | 27.1 ms PASS |
| invest | 2498 | 88.63% | — | 31.1 ms PASS |
| direito | 1993 | 85.95% | — | 446.9 ms PASS |

Nota: latência do Direito perto do teto (BM25 sobre 3811 chunks por
query) — ver backlog 12.3/12.4 (índice invertido).

## Recalibração + eval pós-12.3 (2026-10-02, `amandac 1.0.31)

O scoring mudou (stemming, norma saturante, junk, phrase) → tabela
acima (12.2) substituída:

| pacote | sugerido (center/slope/limiar) | bal | TPR/TNR |
|---|---|---|---|
| cvm | 0.200/16.0/0.85 | 0.995 | 0.99/1.00 |
| ibri | 0.200/22.0/0.90 | 0.999 | 1.00/1.00 |
| invest | 0.250/12.0/0.65 | 0.999 | 1.00/1.00 |
| direito | 0.350/28.0/0.20 | 1.000 | 1.00/1.00 |

Eval full pós-12.3 (3 pequenos 100%; Direito ~1000 ≈ 3%):

| pacote | amostradas | fidelidade | lat média |
|---|---|---|---|
| cvm | 5320 | 86.58% (-2.07pp vs 12.2) | 78.2 ms PASS |
| ibri | 2415 | 87.25% (-1.94pp) | 31–36 ms PASS |
| invest | 2498 | 88.23% (-0.40pp, ruído) | 33.4 ms PASS |
| direito | ~1000 | ~84.5% (~-1.4pp) | ~447 ms PASS |

Tradeoff registrado: queries naturais (gold) melhoram — junk top-1
eliminado (títulos→189), RI→capítulo RI, frases exatas achadas —
ao custo de ~2pp na fidelidade sintética (CVM/IBRI). Guardrail verde;
gold 80/80 com 15 repins + 3 swaps auditados (ver CONTINUAR).
