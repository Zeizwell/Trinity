// Потоковый разбор JSON (json.h). Автомат по байту: значение / после значения / ключ / двоеточие / строка / число / слово.
#include <string.h>
#include <stdlib.h>
#include "json.h"

enum { S_VALUE, S_AFTER, S_KEY, S_COLON, S_STR, S_NUM, S_WORD };

void json_init(json_t *j, json_fn fn, void *user) {
    memset(j, 0, sizeof *j);
    j->fn = fn, j->user = user;
    j->st = S_VALUE;
}

const char *json_key(const json_t *j, int k) {
    if (k < 0) k += j->depth;
    return k >= 0 && k < j->depth && !j->lv[k].arr ? j->lv[k].key : "";
}

int32_t json_idx(const json_t *j, int k) {
    if (k < 0) k += j->depth;
    return k >= 0 && k < j->depth && j->lv[k].arr ? j->lv[k].index : -1;
}

bool json_is(const json_t *j, int k, const char *key) { return !strcmp(json_key(j, k), key); }

// Начало значения: в массиве - следующий номер
static void value_start(json_t *j) {
    if (j->depth && j->lv[j->depth - 1].arr) j->lv[j->depth - 1].index++;
}

// Открыть контейнер: событие (путь - сам контейнер), уровень в стек
static void open(json_t *j, bool arr) {
    value_start(j);
    j->fn(j, arr ? JSON_BEGIN_ARR : JSON_BEGIN_OBJ);
    if (j->depth >= JSON_DEPTH) { j->error = true; return; }
    json_level_t *l = &j->lv[j->depth++];
    l->arr = arr, l->index = -1, l->key[0] = 0;
    j->st = arr ? S_VALUE : S_KEY;
}

static void close_(json_t *j, bool arr) {
    if (!j->depth || j->lv[j->depth - 1].arr != arr) { j->error = true; return; }
    j->depth--;
    j->fn(j, arr ? JSON_END_ARR : JSON_END_OBJ);
    j->st = S_AFTER;
}

static void tok_add(json_t *j, char c) {
    if (j->n < JSON_STR - 1) j->tok[j->n++] = c;
}

// Конец строки: ключ - в уровень, значение - событие
static void str_end(json_t *j) {
    j->tok[j->n] = 0;
    if (j->in_key) {
        strncpy(j->lv[j->depth - 1].key, j->tok, JSON_KEY - 1);
        j->lv[j->depth - 1].key[JSON_KEY - 1] = 0;
        j->st = S_COLON;
        return;
    }
    memcpy(j->str, j->tok, (size_t)j->n + 1);
    j->fn(j, JSON_STRING);
    j->st = S_AFTER;
}

static void num_end(json_t *j) {
    j->tok[j->n] = 0;
    j->num = strtod(j->tok, NULL);
    j->fn(j, JSON_NUM);
    j->st = S_AFTER;
}

static void word_end(json_t *j) {
    j->tok[j->n] = 0;
    j->fn(j, j->tok[0] == 't' ? JSON_TRUE : j->tok[0] == 'f' ? JSON_FALSE : JSON_NULL);
    j->st = S_AFTER;
}

static void byte(json_t *j, char c) {
    switch (j->st) {
    case S_STR:
        if (j->esc) {
            j->esc = false;
            tok_add(j, c == 'n' ? '\n' : c == 't' ? '\t' : c == 'u' ? '?' : c);   // \uXXXX - «?» (цифры - как текст)
        } else if (c == '\\') j->esc = true;
        else if (c == '"') str_end(j);
        else tok_add(j, c);
        return;
    case S_NUM:
        if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E') { tok_add(j, c); return; }
        num_end(j);
        break;                                              // этот байт - уже после числа
    case S_WORD:
        if (c >= 'a' && c <= 'z') { tok_add(j, c); return; }
        word_end(j);
        break;
    }
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t') return;
    switch (j->st) {
    case S_KEY:
        if (c == '"') { j->n = 0, j->in_key = true, j->st = S_STR; return; }
        if (c == '}') { close_(j, false); return; }
        break;
    case S_COLON:
        if (c == ':') { j->st = S_VALUE; return; }
        break;
    case S_AFTER:
        if (c == ',') { j->st = j->depth && !j->lv[j->depth - 1].arr ? S_KEY : S_VALUE; return; }
        if (c == '}') { close_(j, false); return; }
        if (c == ']') { close_(j, true); return; }
        break;
    case S_VALUE:
        if (c == '{') { open(j, false); return; }
        if (c == '[') { open(j, true); return; }
        if (c == ']') { close_(j, true); return; }          // пустой массив
        value_start(j);
        j->n = 0;
        if (c == '"') { j->in_key = false, j->st = S_STR; return; }
        if ((c >= '0' && c <= '9') || c == '-') { tok_add(j, c), j->st = S_NUM; return; }
        if (c >= 'a' && c <= 'z') { tok_add(j, c), j->st = S_WORD; return; }
        break;
    }
    j->error = true;
}

void json_feed(json_t *j, const uint8_t *p, uint32_t n) {
    for (uint32_t i = 0; i < n && !j->error; i++) byte(j, (char)p[i]);
}
