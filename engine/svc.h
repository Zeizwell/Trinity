// Служебный уровень API карты (doc/trinity-api.md): слоты, сервисы, задания, байт статуса. Переносимый C, без
// железа - тот же код в прошивке RP (ядро 0, уровень «Служебное») и в Unreal / тесте на ПК.
//   svc_packet - разобрать служебный пакет из шины (ayfx_bus_t.sbuf) и дать ответ;
//   svc_step   - кусок работы текущего задания (звать в цикле ядра 0; не ждёт, не блокирует);
//   svc_status - биты 5..0 байта статуса (биты 7 / 6 - занято / ответ готов - ставит шина).
// Новый сервис - функции start / step и строка в таблице svc_list (svc.c).
#pragma once
#include <stdint.h>
#include <stdbool.h>

// ---- коды служебных пакетов (trinity-api.md п.2)
enum {
    SVC_P_SLOT_NEW = 0x80, SVC_P_SLOT_WRITE, SVC_P_SLOT_READ, SVC_P_SLOT_INFO, SVC_P_SLOT_FREE, SVC_P_SLOT_LIST,
    SVC_P_LIST = 0x90, SVC_P_CALL,
    SVC_P_JOB_STATUS = 0xA0, SVC_P_JOB_LOG, SVC_P_JOB_STOP,
    SVC_P_CFG_GET = 0xB0, SVC_P_CFG_SET, SVC_P_CFG_SAVE,
    SVC_P_MODE = 0xC0,
};

// ---- состояние чипа (биты 1..0 статуса) и задания (биты 3..2)
enum { SVC_CHIP_AY = 0, SVC_CHIP_PLAY, SVC_CHIP_UPDATE, SVC_CHIP_TEST };
enum { SVC_JOB_NONE = 0, SVC_JOB_RUN, SVC_JOB_DONE, SVC_JOB_ERR };

#define SVC_SLOTS     16            // слотов (номера 1..SVC_SLOTS; 0 - нет)
#define SVC_SLOT_CFG  255           // слот конфига этой RP (план: флеш)
#define SVC_IO_MAX    512           // байт за один SLOT_WRITE / SLOT_READ
#define SVC_REPLY_MAX (2 + 1 + SVC_IO_MAX)   // ответ: длина 2, данные
#define SVC_LOG_SIZE  8192          // лог задания - текстовый слот 'LOG ', строка на событие («потеряно ...»)

typedef struct {
    uint32_t type;                  // тег, 4 знака ('FT2C' и т.п.; первый знак - младший байт)
    uint32_t off, size;             // где в пуле, сколько; size = 0 - слот свободен
} svc_slot_t;

typedef struct svc svc_t;

typedef struct {                    // задание - то, что идёт сейчас (одно на RP)
    uint8_t  state;                 // SVC_JOB_*
    uint8_t  progress;              // 0..100
    uint8_t  svc, cmd, in, out;     // сервис, команда, слот входа, слот выхода
    uint8_t  par[8];                // параметры вызова
    uint32_t pos;                   // где сервис остановился (его дело)
    uint8_t  log;                   // слот лога (текст: что не так, итог); 0 - не было места
    uint32_t log_n;                 // байт в нём
} svc_job_t;

typedef struct {                    // сервис - строка таблицы (trinity-api.md п.5)
    uint8_t  id, ver;
    char     name[16];
    uint32_t in, out;               // типы слотов входа / выхода (0 - любой / нет)
    bool   (*start)(svc_t *s, svc_job_t *j);   // проверить вход, подготовиться; false - ошибка (текст - в лог)
    int    (*step)(svc_t *s, svc_job_t *j);    // кусок работы: 0 - ещё, 1 - готово, -1 - ошибка
    uint8_t (*cmd)(svc_t *s, svc_job_t *j, uint8_t cmd, const uint8_t *par);   // команда идущему заданию (SVC_CALL
                                    //   того же сервиса): 0 - принята, 4 - нет такой; NULL - команд нет (занято)
} svc_def_t;

struct svc {
    uint8_t   *pool;                // пул данных слотов (временные; даёт хозяин: прошивка - ОЗУ, ПК - malloc)
    uint32_t   pool_size;
    svc_slot_t slot[SVC_SLOTS + 1]; // [0] не используется
    svc_job_t  job;
    uint8_t    chip;                // состояние чипа (SVC_CHIP_*) - ставит хозяин
    bool       fx, error;           // FX включены, ошибка карты - ставит хозяин
    uint8_t  (*mode_ay)(void *user);   // MODE AY (стоп самоплеера): 0 - сделано (хозяин)
    const svc_def_t *host;          // сервисы хозяина (плеер Atari - только в прошивке RP-AY): таблица, строк
    int        host_n;
    void      *user;
    uint8_t    reply[SVC_REPLY_MAX];
};

void     svc_init(svc_t *s, uint8_t *pool, uint32_t pool_size);
uint16_t svc_packet(svc_t *s, const uint8_t *pkt);   // pkt: код, длина 2, данные -> байт ответа в s->reply (0 - нет)
void     svc_step(svc_t *s);                         // кусок работы задания
uint8_t  svc_status(const svc_t *s);                 // биты 5..0 байта статуса

// для сервисов
uint8_t *svc_slot_data(svc_t *s, uint8_t n);         // данные слота (NULL - нет такого)
uint32_t svc_slot_size(const svc_t *s, uint8_t n);
uint8_t  svc_slot_new(svc_t *s, uint32_t type, uint32_t size);   // -> номер, 0 - нет места
void     svc_log(svc_t *s, const char *text);        // строка в лог текущего задания (слот 'LOG ')
