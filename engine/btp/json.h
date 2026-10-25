// Потоковый разбор JSON (без дерева в памяти): байты по одному -> события «значение по пути». Нужен, потому что
// .btp распакован - мегабайты, а у RP-AY 520К ОЗУ (FT2 doc/formats/btp.md). Путь - стек: ключ объекта или номер
// элемента массива на каждом уровне. Строки длиннее JSON_STR - обрезаются (имена, волны таймера - короткие).
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define JSON_DEPTH  16
#define JSON_KEY    24
#define JSON_STR    64

enum { JSON_NUM = 1, JSON_STRING, JSON_TRUE, JSON_FALSE, JSON_NULL,    // значения
       JSON_BEGIN_OBJ, JSON_END_OBJ, JSON_BEGIN_ARR, JSON_END_ARR };   // границы (путь - сам контейнер)

typedef struct {
    bool     arr;                   // массив (index) или объект (key)
    int32_t  index;                 // номер элемента массива (-1 - ещё ни одного)
    char     key[JSON_KEY];         // ключ текущего поля объекта
} json_level_t;

typedef struct json json_t;
typedef void (*json_fn)(json_t *j, int type);   // событие: путь - j->lv[0..depth-1], значение - j->num / j->str

struct json {
    json_level_t lv[JSON_DEPTH];
    int      depth;
    double   num;                   // JSON_NUM
    char     str[JSON_STR];         // JSON_STRING
    // разбор
    uint8_t  st;                    // состояние автомата
    bool     esc, in_key;           // экранирование в строке; строка - ключ
    int      n;                     // длина накопленного (строка / число / слово)
    char     tok[JSON_STR];
    bool     error;
    json_fn  fn;
    void    *user;
};

void json_init(json_t *j, json_fn fn, void *user);
void json_feed(json_t *j, const uint8_t *p, uint32_t n);     // байты подряд (кусками любого размера)

// Помощники для обработчика: уровень k пути - ключ / номер (k < 0 - с конца: -1 - последний)
const char *json_key(const json_t *j, int k);
int32_t     json_idx(const json_t *j, int k);
bool        json_is(const json_t *j, int k, const char *key);  // уровень k - объект с ключом key
