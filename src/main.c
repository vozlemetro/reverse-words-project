#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
 
#define MAX_LENGTH 1000
#define MAX_WORDS 100
 
void reverse_words(const char *input, char *output) {
    char *words[MAX_WORDS];
    char temp[MAX_LENGTH];
    int word_count = 0;
 
    strcpy(temp, input);
 
    char *token = strtok(temp, " \t\n");
    while (token != NULL && word_count < MAX_WORDS) {
        words[word_count] = malloc(strlen(token) + 1);
        strcpy(words[word_count], token);
        word_count++;
        token = strtok(NULL, " \t\n");
    }
 
    output[0] = '\0';
    for (int i = word_count - 1; i >= 0; i--) {
        strcat(output, words[i]);
        if (i > 0) {
            strcat(output, " ");
        }
    }
 
    for (int i = 0; i < word_count; i++) {
        free(words[i]);
    }
}
 
int main() {
    char input[MAX_LENGTH];
    char output[MAX_LENGTH];
 
    printf("Введите строку (максимум %d символов): ", MAX_LENGTH - 1);
 
    if (fgets(input, MAX_LENGTH, stdin) == NULL) {
        fprintf(stderr, "Ошибка: не удалось прочитать строку\n");
        return 1;
    }
 
    size_t len = strlen(input);
    if (len > 0 && input[len - 1] == '\n') {
        input[len - 1] = '\0';
    }
 
    if (strlen(input) == 0) {
        fprintf(stderr, "Ошибка: строка не может быть пустой\n");
        return 1;
    }
 
    reverse_words(input, output);
 
    printf("Результат: %s\n", output);
 
    return 0;
}
