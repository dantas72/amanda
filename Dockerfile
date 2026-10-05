# Amanda — imagem de producao (multi-stage).
# Build:  docker build -t amandac .
# Serve:  docker run --rm -p 8080:8080 -v /seus/amanda:/data amandac
# MCP:    docker run --rm -i -v /seus/amanda:/data amandac mcp --package /data/livro.amanda
# Multi:  -v dir com N .amanda -> entrypoint serve todos (model = basename).
# Sem TLS proprio: producao atras de reverse-proxy (ver docs/docker.md).

FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc cmake make ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt version.bin ./
COPY include/ include/
COPY src/ src/
COPY tests/ tests/
COPY examples/ examples/
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
 && cmake --build build --config Release \
 && ./build/amanda_tests \
 && mkdir -p /out/bin && cp build/amandac /out/bin/amandac \
 && ls -l /out/bin/amandac

FROM debian:bookworm-slim AS runtime
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && useradd -r -u 10001 -d /data -s /usr/sbin/nologin amanda \
    && mkdir -p /data && chown amanda:amanda /data
COPY --from=build /out/bin/amandac /usr/local/bin/amandac
COPY scripts/docker-entrypoint.sh /usr/local/bin/docker-entrypoint.sh
RUN chmod +x /usr/local/bin/docker-entrypoint.sh
USER amanda
VOLUME /data
EXPOSE 8080
ENTRYPOINT ["docker-entrypoint.sh"]
CMD ["serve"]
