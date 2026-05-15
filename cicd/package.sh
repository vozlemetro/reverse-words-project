#!/bin/bash

echo "==== ЭТАП УПАКОВКИ ===="

make deb

if [ -f build/reverse-words-project.deb ]; then
    echo "Пакет успешно создан: build/reverse-words-project.deb"
else
    echo "Ошибка: пакет не был создан"
    exit 1
fi
