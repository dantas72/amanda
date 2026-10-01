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
