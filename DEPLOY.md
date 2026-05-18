# DEPLOY.md — Развёртывание ПР4 пошагово

Этот файл — чеклист, по которому можно копировать команды в терминал, чтобы получить рабочее развёртывание в Kubernetes с Prometheus.

> Все команды рассчитаны на **Windows + Git Bash / PowerShell** + **Docker Desktop**. На Linux/macOS будет ровно то же самое.

---

## 0. Требования

Установить (если ещё нет):

- **Docker Desktop** — https://www.docker.com/products/docker-desktop/
- **minikube** — https://minikube.sigs.k8s.io/docs/start/
- **kubectl** — https://kubernetes.io/docs/tasks/tools/
- **Helm** — https://helm.sh/docs/intro/install/

Проверка:
```bash
docker --version
minikube version
kubectl version --client
helm version
```

---

## 1. Поднять кластер minikube

```bash
minikube start --driver=docker --memory=4096 --cpus=2
kubectl get nodes
```

Должна появиться одна нода в статусе **Ready**.

---

## 2. Собрать Docker-образ внутри minikube

Чтобы Kubernetes мог использовать локально собранный образ без пуша в registry, мы переключаем `docker` CLI на демон minikube.

### Git Bash:
```bash
eval $(minikube docker-env)
docker build -t reverse-words:local ./reverse-words-project
```

### PowerShell:
```powershell
& minikube -p minikube docker-env --shell powershell | Invoke-Expression
docker build -t reverse-words:local .\reverse-words-project
```

Проверка:
```bash
docker images | grep reverse-words
```

---

## 3. Установить Prometheus (kube-prometheus-stack)

```bash
helm repo add prometheus-community https://prometheus-community.github.io/helm-charts
helm repo update

kubectl create namespace monitoring

helm install monitoring prometheus-community/kube-prometheus-stack \
  --namespace monitoring \
  --set prometheus.prometheusSpec.serviceMonitorSelectorNilUsesHelmValues=false
```

Параметр `serviceMonitorSelectorNilUsesHelmValues=false` важен — без него Prometheus подбирает только ServiceMonitor'ы из своего же релиза.

Подождать, пока всё поднимется:
```bash
kubectl -n monitoring get pods -w
```

Должны быть в `Running`: `prometheus-monitoring-kube-prometheus-prometheus-0`, `monitoring-kube-prometheus-operator-...`, `monitoring-grafana-...`.

---

## 4. Применить манифесты приложения

```bash
kubectl apply -f reverse-words-project/k8s/deployment.yaml
kubectl apply -f reverse-words-project/k8s/service.yaml
kubectl apply -f reverse-words-project/k8s/servicemonitor.yaml
```

Проверка:
```bash
kubectl get deploy,svc,pod -l app=reverse-words
kubectl get servicemonitor reverse-words
```

Ожидаем 2 Pod'а в статусе **Running**, 0 рестартов.

Если Pod падает — посмотреть логи:
```bash
kubectl logs -l app=reverse-words --tail=50
kubectl describe pod -l app=reverse-words
```

---

## 5. Проверить доступность приложения

```bash
minikube service reverse-words --url
```

Команда выдаст URL вроде `http://192.168.49.2:30080`. Подставьте его:

```bash
URL=$(minikube service reverse-words --url)
curl "$URL/health"
curl "$URL/reverse?text=hello+world"
curl "$URL/reverse?text=one+two+three+four"
curl -X POST --data "alpha beta gamma" "$URL/reverse"
curl "$URL/metrics" | head -30
```

Ожидаемые ответы:
- `/health` → `ok`
- `/reverse?text=hello+world` → `world hello`
- `/metrics` → текст в Prometheus exposition format

---

## 6. Проверить, что Prometheus собирает метрики

Открыть UI Prometheus (port-forward):
```bash
kubectl port-forward -n monitoring svc/monitoring-kube-prometheus-prometheus 9090:9090
```

Открыть в браузере: http://localhost:9090

**Status → Targets** — там должен быть `serviceMonitor/default/reverse-words/0` со статусом **UP** (2 endpoint'а — по одному на Pod).

**Graph** — вписать запрос и нажать Execute:
- `reverse_words_requests_total` — счётчик запросов;
- `reverse_words_processed_words_total` — обработанные слова;
- `rate(reverse_words_requests_total[1m])` — RPS;
- `process_uptime_seconds` — uptime;
- `histogram_quantile(0.95, sum(rate(reverse_words_request_duration_seconds_bucket[1m])) by (le))` — p95 latency.

Сначала сгенерируйте трафик в другом терминале:
```bash
URL=$(minikube service reverse-words --url)
for i in $(seq 1 100); do curl -s "$URL/reverse?text=test+$i" >/dev/null; done
```

После этого графики начнут показывать значения.

---

## 7. Скриншоты для отчёта

Сделать скриншоты:

1. `kubectl get pods` — поды приложения и Prometheus.
2. `kubectl get svc` — Service'ы.
3. Терминал с `curl` запросами и ответами (вкл. `/reverse`, `/health`, `/metrics`).
4. Prometheus UI → **Status → Targets** (видно `reverse-words` UP).
5. Prometheus UI → **Graph** с метрикой `reverse_words_requests_total` (с графиком после трафика).
6. (бонус) Grafana — `minikube service monitoring-grafana -n monitoring --url`, логин `admin`, пароль `prom-operator` (или получить: `kubectl -n monitoring get secret monitoring-grafana -o jsonpath="{.data.admin-password}" | base64 -d`).

---

## 8. Очистка после демонстрации

```bash
kubectl delete -f reverse-words-project/k8s/
helm uninstall monitoring -n monitoring
kubectl delete namespace monitoring
minikube stop
# опционально:
minikube delete
```

---

## Возможные проблемы и решения

| Симптом | Причина | Решение |
|---------|---------|---------|
| Pod в `ImagePullBackOff` | Не выполнен `eval $(minikube docker-env)` перед `docker build` | Повторить шаг 2. Проверка: `minikube ssh -- docker images \| grep reverse-words` |
| Pod в `CrashLoopBackOff` | Бинарник упал при старте | `kubectl logs <pod>` — посмотреть причину |
| `kubectl get servicemonitor` пишет `error: the server doesn't have a resource type "servicemonitor"` | CRD не установлены | Установить kube-prometheus-stack (шаг 3) |
| Prometheus не видит таргет | Не совпал label `release` в ServiceMonitor | Поправить `release: monitoring` в `servicemonitor.yaml` под имя релиза Helm |
| `minikube service` зависает | Docker driver на Windows иногда требует туннеля | Использовать `kubectl port-forward svc/reverse-words 8080:80` |
