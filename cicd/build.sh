#!/bin/bash

echo "==== ЭТАП СБОРКИ ===="

make clean
make reverse-words

if [ -f reverse-words ]; then
    echo "Сборка успешна: создан файл reverse-words"
else
    echo "Ошибка сборки: файл reverse-words не найден"
    exit 1
fi
