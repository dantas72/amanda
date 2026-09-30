# Formato `.amanda` (Fase 1 — binário, little-endian)

```
offset  campo
0       magic[4] = "AMND"
4       u32 format_version = 1
8       u32 app_major, 12 minor, 16 patch
20      str titulo | str autor | str data | str idioma
        str = u32 len + bytes (sem NUL)
...     u32 num_chunks
        para cada chunk:
          str texto | i32 pag_ini | i32 pag_fim | str hash | i32 num_tokens
...     u32 dim (384) | u32 num_vetores (= num_chunks)
        float32[num*dim] embeddings L2-normalizados
...     u32 num_perguntas
        para cada pergunta:
          u32 tipo (0=choice 1=score 2=noul)
          str enunciado
          u32 num_opcoes + str[] opcoes
          f32 min | f32 max
          str afirmacao | i32 pagina_fonte | str chunk_hash
fim-4   u32 crc32_ieee de todos os bytes anteriores
```

Propriedades: detecção de corrupção por CRC, leitura portátil,
compatível Windows/Linux/Mac (inteiros little-endian, floats IEEE754).
