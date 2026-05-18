/*
 * reverse-words HTTP service
 * ---------------------------
 * Простой HTTP-сервер на BSD-сокетах, который реализует:
 *   GET  /health                  - liveness/readiness probe
 *   GET  /reverse?text=<строка>   - возвращает слова в обратном порядке
 *   POST /reverse                 - то же, но строка в теле запроса
 *   GET  /metrics                 - метрики в Prometheus exposition format
 *
 * Однопоточный accept-цикл: все запросы обрабатываются последовательно.
 * Этого достаточно для учебного приложения и убирает гонки данных
 * без необходимости использовать мьютексы.
 *
 * Вариант 6: "Обратный порядок слов"
 * Дисциплина: ТРРиСПО
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define LISTEN_PORT      8080
#define LISTEN_BACKLOG   16
#define MAX_REQUEST      8192
#define MAX_LINE         1024
#define MAX_WORDS        256

/* ----------- Глобальные счётчики (метрики Prometheus) ----------- */
static unsigned long long g_requests_total       = 0;  /* запросов на /reverse */
static unsigned long long g_errors_total         = 0;  /* ошибочных запросов */
static unsigned long long g_processed_words      = 0;  /* всего обработано слов */
static unsigned long long g_health_checks_total  = 0;  /* /health hits */
static time_t             g_start_time           = 0;  /* time(NULL) при старте */

/* Гистограмма длительностей /reverse (бакеты в секундах). */
static const double H_BUCKETS[] = {0.0001, 0.001, 0.01, 0.1, 1.0};
#define H_BUCKET_COUNT (sizeof(H_BUCKETS) / sizeof(H_BUCKETS[0]))
static unsigned long long g_hist_bucket[H_BUCKET_COUNT] = {0};
static unsigned long long g_hist_inf    = 0;
static double             g_hist_sum    = 0.0;
static unsigned long long g_hist_count  = 0;

/* Флаг для graceful shutdown (выставляется обработчиком сигналов). */
static volatile sig_atomic_t g_should_stop = 0;
static int g_listen_fd = -1;

static void on_signal(int sig) {
    (void)sig;
    g_should_stop = 1;
    if (g_listen_fd >= 0) {
        close(g_listen_fd);
        g_listen_fd = -1;
    }
}

/* ----------------------- Основной алгоритм ----------------------- */
/*
 * Переставляет слова входной строки в обратном порядке.
 * Реализация совместима с тем, что было в ПР1.
 */
static void reverse_words(const char *input, char *output, size_t out_size) {
    char *words[MAX_WORDS];
    char buf[MAX_LINE];
    int word_count = 0;
    size_t i;

    output[0] = '\0';

    /* Копируем во временный буфер, чтобы можно было использовать strtok */
    strncpy(buf, input, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *saveptr = NULL;
    char *token = strtok_r(buf, " \t\r\n", &saveptr);
    while (token != NULL && word_count < MAX_WORDS) {
        words[word_count] = strdup(token);
        if (words[word_count] == NULL) {
            break; /* при OOM просто прерываемся, всё что собрали — освободим ниже */
        }
        word_count++;
        token = strtok_r(NULL, " \t\r\n", &saveptr);
    }

    /* Сборка результата в обратном порядке */
    size_t used = 0;
    for (int j = word_count - 1; j >= 0 && used < out_size - 1; j--) {
        size_t wlen = strlen(words[j]);
        if (used + wlen + 1 >= out_size) break;
        memcpy(output + used, words[j], wlen);
        used += wlen;
        if (j > 0 && used < out_size - 1) {
            output[used++] = ' ';
        }
    }
    output[used] = '\0';

    /* Учёт количества слов в метрике */
    g_processed_words += (unsigned long long)word_count;

    /* Освобождение памяти */
    for (i = 0; i < (size_t)word_count; i++) free(words[i]);
}

/* ----------------------- Утилиты HTTP ----------------------- */

/* Декодирование %XX и '+' в query-параметре. Пишет в out. */
static void url_decode(const char *src, char *out, size_t out_size) {
    size_t i = 0;
    while (*src && i + 1 < out_size) {
        if (*src == '+') {
            out[i++] = ' ';
            src++;
        } else if (*src == '%' && src[1] && src[2]) {
            char hex[3] = {src[1], src[2], 0};
            char *endp = NULL;
            long v = strtol(hex, &endp, 16);
            if (endp == hex + 2) {
                out[i++] = (char)v;
                src += 3;
            } else {
                out[i++] = *src++;
            }
        } else {
            out[i++] = *src++;
        }
    }
    out[i] = '\0';
}

/* Ищет query-параметр name=... в строке query. Возвращает 1 если нашёл. */
static int find_query_param(const char *query, const char *name, char *out, size_t out_size) {
    if (query == NULL) return 0;
    size_t nlen = strlen(name);
    const char *p = query;
    while (*p) {
        if (strncmp(p, name, nlen) == 0 && p[nlen] == '=') {
            const char *val = p + nlen + 1;
            const char *end = strchr(val, '&');
            size_t vlen = end ? (size_t)(end - val) : strlen(val);
            char raw[MAX_LINE];
            if (vlen >= sizeof(raw)) vlen = sizeof(raw) - 1;
            memcpy(raw, val, vlen);
            raw[vlen] = '\0';
            url_decode(raw, out, out_size);
            return 1;
        }
        /* перейти к следующему параметру */
        const char *amp = strchr(p, '&');
        if (!amp) break;
        p = amp + 1;
    }
    return 0;
}

static void send_response(int fd, int status, const char *status_text,
                          const char *content_type, const char *body, size_t body_len) {
    char header[512];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, status_text, content_type, body_len);
    if (hlen > 0) {
        ssize_t w = write(fd, header, (size_t)hlen);
        (void)w;
    }
    if (body && body_len > 0) {
        ssize_t w = write(fd, body, body_len);
        (void)w;
    }
}

static void send_text(int fd, int status, const char *status_text, const char *body) {
    send_response(fd, status, status_text, "text/plain; charset=utf-8", body, strlen(body));
}

/* ----------------------- /metrics ----------------------- */

static void handle_metrics(int fd) {
    char body[4096];
    size_t off = 0;
    time_t uptime = time(NULL) - g_start_time;

    off += snprintf(body + off, sizeof(body) - off,
        "# HELP reverse_words_requests_total Total number of /reverse requests.\n"
        "# TYPE reverse_words_requests_total counter\n"
        "reverse_words_requests_total %llu\n",
        g_requests_total);

    off += snprintf(body + off, sizeof(body) - off,
        "# HELP reverse_words_errors_total Total number of /reverse errors.\n"
        "# TYPE reverse_words_errors_total counter\n"
        "reverse_words_errors_total %llu\n",
        g_errors_total);

    off += snprintf(body + off, sizeof(body) - off,
        "# HELP reverse_words_processed_words_total Total number of processed words.\n"
        "# TYPE reverse_words_processed_words_total counter\n"
        "reverse_words_processed_words_total %llu\n",
        g_processed_words);

    off += snprintf(body + off, sizeof(body) - off,
        "# HELP reverse_words_health_checks_total Total number of /health requests.\n"
        "# TYPE reverse_words_health_checks_total counter\n"
        "reverse_words_health_checks_total %llu\n",
        g_health_checks_total);

    off += snprintf(body + off, sizeof(body) - off,
        "# HELP process_uptime_seconds Process uptime in seconds.\n"
        "# TYPE process_uptime_seconds gauge\n"
        "process_uptime_seconds %lld\n",
        (long long)uptime);

    /* Гистограмма */
    off += snprintf(body + off, sizeof(body) - off,
        "# HELP reverse_words_request_duration_seconds /reverse request duration.\n"
        "# TYPE reverse_words_request_duration_seconds histogram\n");
    unsigned long long cumulative = 0;
    for (size_t i = 0; i < H_BUCKET_COUNT; i++) {
        cumulative += g_hist_bucket[i];
        off += snprintf(body + off, sizeof(body) - off,
            "reverse_words_request_duration_seconds_bucket{le=\"%g\"} %llu\n",
            H_BUCKETS[i], cumulative);
    }
    cumulative += g_hist_inf;
    off += snprintf(body + off, sizeof(body) - off,
        "reverse_words_request_duration_seconds_bucket{le=\"+Inf\"} %llu\n"
        "reverse_words_request_duration_seconds_sum %.6f\n"
        "reverse_words_request_duration_seconds_count %llu\n",
        cumulative, g_hist_sum, g_hist_count);

    send_response(fd, 200, "OK", "text/plain; version=0.0.4; charset=utf-8", body, off);
}

/* ----------------------- /reverse ----------------------- */

static void record_duration(double seconds) {
    int placed = 0;
    for (size_t i = 0; i < H_BUCKET_COUNT; i++) {
        if (seconds <= H_BUCKETS[i]) {
            g_hist_bucket[i]++;
            placed = 1;
            break;
        }
    }
    if (!placed) g_hist_inf++;
    g_hist_sum += seconds;
    g_hist_count++;
}

static void handle_reverse(int fd, const char *text) {
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    g_requests_total++;

    if (text == NULL || text[0] == '\0') {
        g_errors_total++;
        send_text(fd, 400, "Bad Request", "error: empty input\n");
        return;
    }

    char output[MAX_LINE];
    reverse_words(text, output, sizeof(output));

    /* Добавим перевод строки для удобства curl-вывода */
    size_t olen = strlen(output);
    if (olen + 1 < sizeof(output)) {
        output[olen] = '\n';
        output[olen + 1] = '\0';
    }

    send_text(fd, 200, "OK", output);

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double dt = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
    record_duration(dt);
}

/* ----------------------- Маршрутизация ----------------------- */

static void handle_request(int fd) {
    char req[MAX_REQUEST];
    ssize_t total = 0;

    /* Читаем сколько есть, до заголовков точно влезет */
    while (total < (ssize_t)sizeof(req) - 1) {
        ssize_t n = read(fd, req + total, sizeof(req) - 1 - (size_t)total);
        if (n <= 0) break;
        total += n;
        /* нашли конец заголовков? для GET этого хватает; для POST читаем дальше */
        if (strstr(req, "\r\n\r\n") != NULL) break;
    }
    if (total <= 0) {
        return;
    }
    req[total] = '\0';

    /* Парсим первую строку: METHOD PATH HTTP/1.x */
    char method[16] = {0}, path[1024] = {0};
    if (sscanf(req, "%15s %1023s", method, path) != 2) {
        send_text(fd, 400, "Bad Request", "bad request line\n");
        return;
    }

    /* Отделяем path от query */
    char *query = strchr(path, '?');
    if (query) { *query = '\0'; query++; }

    if (strcmp(path, "/health") == 0) {
        g_health_checks_total++;
        send_text(fd, 200, "OK", "ok\n");
        return;
    }

    if (strcmp(path, "/metrics") == 0) {
        handle_metrics(fd);
        return;
    }

    if (strcmp(path, "/reverse") == 0) {
        char text[MAX_LINE] = {0};
        int have_text = 0;

        if (strcmp(method, "GET") == 0) {
            have_text = find_query_param(query, "text", text, sizeof(text));
        } else if (strcmp(method, "POST") == 0) {
            /* Найдём тело — после "\r\n\r\n" */
            char *body_start = strstr(req, "\r\n\r\n");
            if (body_start) {
                body_start += 4;
                /* Получим Content-Length, если можем — иначе берём всё после заголовков */
                size_t body_in_buf = (size_t)(total - (body_start - req));
                /* Простая стратегия: используем уже прочитанное тело;
                 * для учебных нужд этого достаточно. */
                size_t copy = body_in_buf < sizeof(text) - 1 ? body_in_buf : sizeof(text) - 1;
                memcpy(text, body_start, copy);
                text[copy] = '\0';
                if (copy > 0) have_text = 1;
            }
        } else {
            send_text(fd, 405, "Method Not Allowed", "method not allowed\n");
            return;
        }

        if (!have_text) {
            g_requests_total++;
            g_errors_total++;
            send_text(fd, 400, "Bad Request",
                      "usage: GET /reverse?text=<urlencoded>\n");
            return;
        }
        handle_reverse(fd, text);
        return;
    }

    if (strcmp(path, "/") == 0) {
        send_text(fd, 200, "OK",
            "reverse-words service\n"
            "endpoints:\n"
            "  GET /health\n"
            "  GET /reverse?text=<string>\n"
            "  POST /reverse (body = string)\n"
            "  GET /metrics\n");
        return;
    }

    send_text(fd, 404, "Not Found", "not found\n");
}

/* ----------------------- main ----------------------- */

int main(void) {
    /* stdout без буферизации — чтобы kubectl logs показывал сразу */
    setvbuf(stdout, NULL, _IONBF, 0);

    g_start_time = time(NULL);

    /* Обработчики сигналов для graceful shutdown в Kubernetes */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    g_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listen_fd < 0) {
        perror("socket");
        return 1;
    }

    int yes = 1;
    setsockopt(g_listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(LISTEN_PORT);

    if (bind(g_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(g_listen_fd);
        return 1;
    }

    if (listen(g_listen_fd, LISTEN_BACKLOG) < 0) {
        perror("listen");
        close(g_listen_fd);
        return 1;
    }

    printf("reverse-words server listening on 0.0.0.0:%d\n", LISTEN_PORT);

    while (!g_should_stop) {
        struct sockaddr_in cli;
        socklen_t clilen = sizeof(cli);
        int cfd = accept(g_listen_fd, (struct sockaddr *)&cli, &clilen);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            if (g_should_stop) break;
            perror("accept");
            continue;
        }
        handle_request(cfd);
        close(cfd);
    }

    if (g_listen_fd >= 0) close(g_listen_fd);
    printf("reverse-words server stopped\n");
    return 0;
}
