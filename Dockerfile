# Multi-stage Dockerfile для reverse-words HTTP service.
# Используется при сборке прямо из исходников (например, внутри
# minikube docker-env) — не требует предварительно собранного .deb.

# ---------- stage 1: build ----------
FROM ubuntu:24.04 AS builder

RUN apt-get update && \
    apt-get install -y --no-install-recommends build-essential && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY Makefile ./
COPY src ./src

RUN make reverse-words

# ---------- stage 2: runtime ----------
FROM ubuntu:24.04

# curl нужен для HEALTHCHECK; libc6 уже есть в базовом образе.
RUN apt-get update && \
    apt-get install -y --no-install-recommends curl && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /src/reverse-words /usr/local/bin/reverse-words

EXPOSE 8080

HEALTHCHECK --interval=10s --timeout=2s --start-period=3s --retries=3 \
    CMD curl -fsS http://127.0.0.1:8080/health || exit 1

# Используем непривилегированного пользователя
RUN useradd -r -u 10001 -s /usr/sbin/nologin appuser
USER appuser

CMD ["reverse-words"]
