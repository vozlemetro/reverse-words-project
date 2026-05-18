# Reverse Words Project

![CI/CD Pipeline](https://github.com/YOUR_USERNAME/reverse-words-project/workflows/CI/CD%20Pipeline/badge.svg)

## Описание

HTTP-сервис на C, который возвращает строку с обратным порядком слов.

**Вариант 6** — Лабораторная работа 2
**Дисциплина:** Технологии распространения, развертывания и сопровождения ПО
**Студент:** Гребенников

### Эволюция проекта по практическим:

- **ПР1** — программа на C + Makefile + сборка `.deb`-пакета.
- **ПР2** — CI/CD на GitHub Actions (build → test → package).
- **ПР3** — Dockerfile, публикация образа в GHCR.
- **ПР4** — программа переписана в HTTP-сервис, Kubernetes-манифесты, Prometheus.

---

## HTTP API

После запуска сервис слушает порт **8080**:

| Метод | Путь | Назначение |
|-------|------|-----------|
| `GET`  | `/health` | health-check, возвращает `ok` |
| `GET`  | `/reverse?text=<urlencoded>` | возвращает слова в обратном порядке |
| `POST` | `/reverse` (тело = строка) | то же, но через POST |
| `GET`  | `/metrics` | метрики в Prometheus text format |
| `GET`  | `/` | краткая справка |

### Примеры
```bash
$ curl 'http://localhost:8080/reverse?text=hello+world'
world hello

$ curl -X POST --data "one two three" http://localhost:8080/reverse
three two one

$ curl http://localhost:8080/metrics
# HELP reverse_words_requests_total Total number of /reverse requests.
# TYPE reverse_words_requests_total counter
reverse_words_requests_total 42
...
```

---

## Структура проекта

```
reverse-words-project/
├── src/
│   └── main.c              # HTTP-сервер (BSD sockets) + reverse_words
├── cicd/
│   ├── build.sh            # Скрипт сборки
│   ├── test.sh             # HTTP-тесты через curl
│   └── package.sh          # Сборка .deb
├── k8s/                    # Kubernetes-манифесты (ПР4)
│   ├── deployment.yaml
│   ├── service.yaml
│   └── servicemonitor.yaml
├── DEBIAN/
│   └── control             # control-файл deb-пакета
├── .github/workflows/
│   └── ci.yml              # GitHub Actions
├── Dockerfile              # Multi-stage сборка образа
├── Makefile
├── DEPLOY.md               # Пошаговое развёртывание в k8s
├── .gitignore
└── README.md
```

---

## Локальная сборка и запуск

```bash
make reverse-words
./reverse-words &           # сервер стартует на :8080
curl http://localhost:8080/health
curl 'http://localhost:8080/reverse?text=hello+world'
```

### Создание deb-пакета
```bash
make deb
# Артефакт: build/reverse-words-project.deb
```

### Запуск тестов
```bash
bash cicd/test.sh
```

---

## Запуск в Docker

```bash
docker build -t reverse-words:local .
docker run -d -p 8080:8080 --name rw reverse-words:local
curl http://localhost:8080/reverse?text=foo+bar
docker rm -f rw
```

---

## Развёртывание в Kubernetes (ПР4)

Полный пошаговый чеклист с командами — в файле [`DEPLOY.md`](./DEPLOY.md).

Краткая схема:

1. `minikube start` — поднять локальный кластер.
2. `eval $(minikube docker-env) && docker build -t reverse-words:local .` — собрать образ внутри minikube.
3. `helm install monitoring prometheus-community/kube-prometheus-stack -n monitoring --create-namespace` — поставить Prometheus.
4. `kubectl apply -f k8s/` — применить Deployment, Service, ServiceMonitor.
5. Сходить за метриками: `kubectl port-forward -n monitoring svc/monitoring-kube-prometheus-prometheus 9090:9090`.

### Что включают манифесты

- **Deployment** — 2 реплики, liveness/readiness на `/health`, resource requests/limits.
- **Service** — тип `NodePort` (порт 30080), порт `http` → 8080.
- **ServiceMonitor** — CRD из Prometheus Operator, указывает скрейпить `/metrics` каждые 15 секунд.

---

## Метрики Prometheus

| Метрика | Тип | Описание |
|---------|-----|----------|
| `reverse_words_requests_total` | counter | число запросов на `/reverse` |
| `reverse_words_errors_total` | counter | число ошибочных запросов |
| `reverse_words_processed_words_total` | counter | всего обработанных слов |
| `reverse_words_health_checks_total` | counter | число `/health` запросов |
| `reverse_words_request_duration_seconds` | histogram | длительность `/reverse` |
| `process_uptime_seconds` | gauge | uptime процесса |

Полезные PromQL-запросы для Grafana/UI:

```promql
# RPS
rate(reverse_words_requests_total[1m])

# Среднее число слов на запрос
increase(reverse_words_processed_words_total[5m])
  / increase(reverse_words_requests_total[5m])

# p95 latency
histogram_quantile(0.95,
  sum(rate(reverse_words_request_duration_seconds_bucket[1m])) by (le))

# Процент ошибок
100 * rate(reverse_words_errors_total[1m])
   / rate(reverse_words_requests_total[1m])
```

---

## CI/CD Pipeline

GitHub Actions:
1. **Build** — сборка `reverse-words`.
2. **Test** — HTTP-тесты через `curl`.
3. **Package** — сборка `.deb`.
4. **Deploy** — публикация Docker-образа в `ghcr.io`.

---

## Требования

- GCC, Make
- dpkg-dev (для `.deb`)
- Docker, minikube, kubectl, helm (для k8s-развёртывания)

## Установка зависимостей (Ubuntu/Debian)

```bash
sudo apt-get update
sudo apt-get install build-essential dpkg-dev curl
```

## Лицензия

Учебный проект для БГТУ "ВОЕНМЕХ".
