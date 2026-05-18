#!/bin/bash
# Тесты для HTTP-сервиса reverse-words.
# Запускает бинарник в фоне, прогоняет curl-запросы, убивает процесс.

set -u

echo "==== ЭТАП ТЕСТИРОВАНИЯ (HTTP) ===="

if [ ! -x ./reverse-words ]; then
    echo "Ошибка: бинарник ./reverse-words не найден или не исполняемый"
    exit 1
fi

# Стартуем сервер в фоне
./reverse-words &
SERVER_PID=$!

cleanup() {
    if kill -0 "$SERVER_PID" 2>/dev/null; then
        kill -TERM "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

# Ждём, пока порт начнёт отвечать (до 5 секунд)
for i in $(seq 1 50); do
    if curl -fsS http://127.0.0.1:8080/health >/dev/null 2>&1; then
        break
    fi
    sleep 0.1
done

fail() {
    echo "ТЕСТ ПРОВАЛЕН: $1"
    exit 1
}

# ---- Тест 1: /health ----
echo "Тест 1: GET /health"
result=$(curl -fsS http://127.0.0.1:8080/health)
echo "  ответ: $result"
echo "$result" | grep -q "ok" || fail "/health не вернул 'ok'"

# ---- Тест 2: /reverse?text=hello+world ----
echo "Тест 2: GET /reverse?text=hello+world"
result=$(curl -fsS "http://127.0.0.1:8080/reverse?text=hello+world")
echo "  ответ: $result"
echo "$result" | grep -q "world hello" || fail "ожидалось 'world hello'"

# ---- Тест 3: одно слово ----
echo "Тест 3: GET /reverse?text=hello"
result=$(curl -fsS "http://127.0.0.1:8080/reverse?text=hello")
echo "  ответ: $result"
echo "$result" | grep -q "hello" || fail "ожидалось 'hello'"

# ---- Тест 4: несколько слов ----
echo "Тест 4: GET /reverse?text=one+two+three+four+five"
result=$(curl -fsS "http://127.0.0.1:8080/reverse?text=one+two+three+four+five")
echo "  ответ: $result"
echo "$result" | grep -q "five four three two one" || fail "ожидалось 'five four three two one'"

# ---- Тест 5: цифры ----
echo "Тест 5: GET /reverse?text=123+456+789"
result=$(curl -fsS "http://127.0.0.1:8080/reverse?text=123+456+789")
echo "  ответ: $result"
echo "$result" | grep -q "789 456 123" || fail "ожидалось '789 456 123'"

# ---- Тест 6: %20 в качестве пробела (URL-encoded) ----
echo "Тест 6: GET /reverse?text=foo%20bar"
result=$(curl -fsS "http://127.0.0.1:8080/reverse?text=foo%20bar")
echo "  ответ: $result"
echo "$result" | grep -q "bar foo" || fail "ожидалось 'bar foo'"

# ---- Тест 7: POST /reverse ----
echo "Тест 7: POST /reverse"
result=$(curl -fsS -X POST --data-raw "alpha beta gamma" http://127.0.0.1:8080/reverse)
echo "  ответ: $result"
echo "$result" | grep -q "gamma beta alpha" || fail "ожидалось 'gamma beta alpha'"

# ---- Тест 8: /metrics ----
echo "Тест 8: GET /metrics"
result=$(curl -fsS http://127.0.0.1:8080/metrics)
echo "$result" | grep -q "^reverse_words_requests_total " || fail "нет метрики reverse_words_requests_total"
echo "$result" | grep -q "^reverse_words_processed_words_total " || fail "нет метрики reverse_words_processed_words_total"
echo "$result" | grep -q "^process_uptime_seconds " || fail "нет метрики process_uptime_seconds"
echo "$result" | grep -q "reverse_words_request_duration_seconds_bucket" || fail "нет гистограммы"
echo "  ОК — метрики присутствуют"

# ---- Тест 9: пустой text → 400 ----
echo "Тест 9: GET /reverse?text= (пустая строка → 400)"
http_code=$(curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:8080/reverse?text=")
echo "  HTTP-код: $http_code"
[ "$http_code" = "400" ] || fail "ожидался 400, получено $http_code"

# ---- Тест 10: неизвестный путь → 404 ----
echo "Тест 10: GET /no-such-route → 404"
http_code=$(curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:8080/no-such-route")
echo "  HTTP-код: $http_code"
[ "$http_code" = "404" ] || fail "ожидался 404, получено $http_code"

echo ""
echo "Все тесты пройдены успешно!"
