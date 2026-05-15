#!/bin/bash

echo "==== ЭТАП ТЕСТИРОВАНИЯ ===="

echo "Тест 1: Обычная строка"
result=$(echo "hello world test" | ./reverse-words | tail -n1)
echo "$result"
if ! echo "$result" | grep -q "test world hello"; then
    echo "Тест 1 провален"; exit 1
fi

echo "Тест 2: Одно слово"
result=$(echo "hello" | ./reverse-words | tail -n1)
echo "$result"
if ! echo "$result" | grep -q "hello"; then
    echo "Тест 2 провален"; exit 1
fi

echo "Тест 3: Несколько слов"
result=$(echo "one two three four five" | ./reverse-words | tail -n1)
echo "$result"
if ! echo "$result" | grep -q "five four three two one"; then
    echo "Тест 3 провален"; exit 1
fi

echo "Тест 4: Строка с цифрами"
result=$(echo "123 456 789" | ./reverse-words | tail -n1)
echo "$result"
if ! echo "$result" | grep -q "789 456 123"; then
    echo "Тест 4 провален"; exit 1
fi

echo "Тест 5: Два слова"
result=$(echo "first second" | ./reverse-words | tail -n1)
echo "$result"
if ! echo "$result" | grep -q "second first"; then
    echo "Тест 5 провален"; exit 1
fi

echo "Все тесты пройдены успешно!"
