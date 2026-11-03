// Служебный уровень API карты (svc.h). Данные в пакетах - младшим байтом вперёд (как Z80).
#include <string.h>
#include <stdio.h>
#include "svc.h"

// ================= сервисы =================

// 4 «тест карты»: CRC32 слота входа кусками по 4К (прогресс), итог - в лог. Образец сервиса
static uint32_t crc_tab[256];

static bool test_start(svc_t *s, svc_job_t *j) {
    if (!svc_slot_data(s, j->in)) { svc_log(s, "test: no input slot"); return false; }
    if (!crc_tab[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            crc_tab[i] = c;
        }
    j->par[0] = j->par[1] = j->par[2] = j->par[3] = 0xFF;  // CRC - в par[0..3]
    return true;
}

static int test_step(svc_t *s, svc_job_t *j) {
    uint32_t size = svc_slot_size(s, j->in), n = size - j->pos;
    const uint8_t *d = svc_slot_data(s, j->in);
    uint32_t c;
    memcpy(&c, j->par, 4);
    if (n > 4096) n = 4096;
    for (uint32_t i = 0; i < n; i++) c = crc_tab[(c ^ d[j->pos + i]) & 0xFF] ^ (c >> 8);
    memcpy(j->par, &c, 4);
    j->pos += n;
    j->progress = (uint8_t)(size ? (uint64_t)j->pos * 100 / size : 100);
    if (j->pos < size) return 0;
    char t[48];
    snprintf(t, sizeof t, "CRC32 %08X, %u bytes", (unsigned)~c, (unsigned)size);
    svc_log(s, t);
    return 1;
}

// Таблица сервисов (trinity-api.md п.5): новый сервис - строка здесь
static const svc_def_t svc_list[] = {
    { 4, 1, "test", 0, 0, test_start, test_step },
};
#define SVC_N (int)(sizeof svc_list / sizeof svc_list[0])

// Сервис по номеру: общие (svc_list), потом хозяина (s->host)
static const svc_def_t *svc_find(const svc_t *s, uint8_t id) {
    for (int i = 0; i < SVC_N; i++) if (svc_list[i].id == id) return &svc_list[i];
    for (int i = 0; i < s->host_n; i++) if (s->host[i].id == id) return &s->host[i];
    return NULL;
}

// ================= слоты =================

uint8_t *svc_slot_data(svc_t *s, uint8_t n) {
    return n >= 1 && n <= SVC_SLOTS && s->slot[n].size ? s->pool + s->slot[n].off : NULL;
}

uint32_t svc_slot_size(const svc_t *s, uint8_t n) {
    return n >= 1 && n <= SVC_SLOTS ? s->slot[n].size : 0;
}

// Место в пуле: первая дыра нужного размера между занятыми слотами (выравнивание 4)
static bool pool_fit(const svc_t *s, uint32_t size, uint32_t *off) {
    uint32_t at = 0;
    for (int again = 1; again; ) {
        again = 0;
        for (int i = 1; i <= SVC_SLOTS; i++) {
            const svc_slot_t *t = &s->slot[i];
            if (t->size && at < t->off + t->size && t->off < at + size) {   // пересекается - за него
                at = (t->off + t->size + 3) & ~3u;
                again = 1;
            }
        }
    }
    if (at + size > s->pool_size) return false;
    *off = at;
    return true;
}

uint8_t svc_slot_new(svc_t *s, uint32_t type, uint32_t size) {
    if (!size) return 0;
    for (uint8_t n = 1; n <= SVC_SLOTS; n++) {
        if (s->slot[n].size) continue;
        if (!pool_fit(s, size, &s->slot[n].off)) return 0;
        s->slot[n].type = type, s->slot[n].size = size;
        memset(s->pool + s->slot[n].off, 0, size);
        return n;
    }
    return 0;
}

// ================= задания =================

// Строка в лог задания (слот 'LOG '): текст + перевод строки. Лог полон - дальше теряется, в конце «...»
void svc_log(svc_t *s, const char *text) {
    svc_job_t *j = &s->job;
    uint8_t *p = svc_slot_data(s, j->log);
    if (!p || j->log_n >= SVC_LOG_SIZE) return;
    uint32_t n = (uint32_t)strlen(text);
    if (j->log_n + n + 1 > SVC_LOG_SIZE - 4) {               // не влезает - «...» и лог закрыт
        memcpy(p + j->log_n, "...\n", 4);
        j->log_n = SVC_LOG_SIZE;
        return;
    }
    memcpy(p + j->log_n, text, n);
    p[j->log_n + n] = '\n';
    j->log_n += n + 1;
}

// Запуск: 0 - пошло, 1 - занято, 2 - нет сервиса, 3 - вход не тот. Тот же сервис уже идёт - команда ему (cmd):
// 0 - принята, 4 - нет такой
static uint8_t job_start(svc_t *s, const uint8_t *d, uint16_t n) {
    svc_job_t *j = &s->job;
    const svc_def_t *def = n >= 4 ? svc_find(s, d[0]) : NULL;
    if (j->state == SVC_JOB_RUN) {
        if (!def || def->id != j->svc || !def->cmd) return 1;
        uint8_t par[sizeof j->par] = { 0 };
        memcpy(par, d + 4, n - 4 > sizeof par ? sizeof par : n - 4u);
        return def->cmd(s, j, d[1], par);
    }
    if (!def) return 2;
    if (j->log) s->slot[j->log].size = 0;                   // лог прошлого задания - освободить
    memset(j, 0, sizeof *j);
    j->svc = d[0], j->cmd = d[1], j->in = d[2], j->out = d[3];
    j->log = svc_slot_new(s, 0x20474F4Cu, SVC_LOG_SIZE);    // 'LOG ' (0 - нет места: задание без лога)
    memcpy(j->par, d + 4, n - 4 > sizeof j->par ? sizeof j->par : n - 4u);
    if (def->in && (!svc_slot_data(s, j->in) || s->slot[j->in].type != def->in)) return 3;
    j->state = def->start(s, j) ? SVC_JOB_RUN : SVC_JOB_ERR;
    return 0;
}

void svc_step(svc_t *s) {
    svc_job_t *j = &s->job;
    if (j->state != SVC_JOB_RUN) return;
    const svc_def_t *def = svc_find(s, j->svc);
    int r = def ? def->step(s, j) : -1;
    if (r > 0) j->state = SVC_JOB_DONE, j->progress = 100;
    else if (r < 0) j->state = SVC_JOB_ERR;
}

// ================= пакеты =================

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void     wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24); }

void svc_init(svc_t *s, uint8_t *pool, uint32_t pool_size) {
    memset(s, 0, sizeof *s);
    s->pool = pool, s->pool_size = pool_size;
}

uint8_t svc_status(const svc_t *s) {
    return (uint8_t)((s->chip & 3) | (s->job.state & 3) << 2 | (s->fx ? 0x10 : 0) | (s->error ? 0x20 : 0));
}

// Пакет -> ответ в s->reply: длина 2, данные. -> байт ответа всего (0 - пакет без ответа)
uint16_t svc_packet(svc_t *s, const uint8_t *pkt) {
    const uint8_t *d = pkt + 3;
    uint16_t n = (uint16_t)(pkt[1] | pkt[2] << 8), r = 0;   // r - байт данных ответа
    uint8_t *o = s->reply + 2;
    switch (pkt[0]) {
    case SVC_P_SLOT_NEW:                                    // тип 4, размер 4, флаги -> номер (0 - нет)
        if (n < 9) break;
        o[r++] = (d[8] & 1) ? 0 : svc_slot_new(s, rd32(d), rd32(d + 4));   // постоянные - план
        break;
    case SVC_P_SLOT_WRITE: {                                // слот, смещение 4, байты
        uint8_t *p = n > 5 ? svc_slot_data(s, d[0]) : NULL;
        uint32_t off = n > 5 ? rd32(d + 1) : 0, k = n - 5u;
        if (p && off <= s->slot[d[0]].size && k <= s->slot[d[0]].size - off) memcpy(p + off, d + 5, k);
        return 0;
    }
    case SVC_P_SLOT_READ: {                                 // слот, смещение 4, сколько 2 -> байты
        uint8_t *p = n >= 7 ? svc_slot_data(s, d[0]) : NULL;
        if (!p) break;
        uint32_t off = rd32(d + 1), size = s->slot[d[0]].size, k = d[5] | d[6] << 8;
        if (off > size) off = size;
        if (k > size - off) k = size - off;
        if (k > SVC_IO_MAX) k = SVC_IO_MAX;
        memcpy(o, p + off, k);
        r = (uint16_t)k;
        break;
    }
    case SVC_P_SLOT_INFO:                                   // слот -> тип 4, размер 4, флаги
        if (n < 1 || !svc_slot_data(s, d[0])) break;
        wr32(o, s->slot[d[0]].type), wr32(o + 4, s->slot[d[0]].size), o[8] = 0;
        r = 9;
        break;
    case SVC_P_SLOT_FREE:
        if (n >= 1 && d[0] >= 1 && d[0] <= SVC_SLOTS) s->slot[d[0]].size = 0;
        return 0;
    case SVC_P_SLOT_LIST:                                   // -> номера занятых
        for (uint8_t i = 1; i <= SVC_SLOTS; i++) if (s->slot[i].size) o[r++] = i;
        break;
    case SVC_P_LIST:                                        // -> число, по 26 байт: номер, версия, имя 16, вход, выход
        o[r++] = (uint8_t)(SVC_N + s->host_n);
        for (int i = 0; i < SVC_N + s->host_n; i++) {
            const svc_def_t *v = i < SVC_N ? &svc_list[i] : &s->host[i - SVC_N];
            o[r++] = v->id, o[r++] = v->ver;
            memcpy(o + r, v->name, 16), r += 16;
            wr32(o + r, v->in), wr32(o + r + 4, v->out), r += 8;
        }
        break;
    case SVC_P_CALL:                                        // сервис, команда, вход, выход, параметры -> итог
        o[r++] = job_start(s, d, n);
        break;
    case SVC_P_JOB_STATUS:                                  // -> состояние, прогресс, слот лога, байт в логе 4
        o[r++] = s->job.state, o[r++] = s->job.progress, o[r++] = s->job.log;
        wr32(o + r, s->job.log_n), r += 4;
        break;
    case SVC_P_JOB_LOG:                                     // -> начало лога (до 512); целиком - SLOT_READ слота лога
        r = (uint16_t)(s->job.log_n < SVC_IO_MAX ? s->job.log_n : SVC_IO_MAX);
        if (r) memcpy(o, svc_slot_data(s, s->job.log), r);
        break;
    case SVC_P_JOB_STOP:
        if (s->job.state == SVC_JOB_RUN) s->job.state = SVC_JOB_ERR, svc_log(s, "stopped");
        return 0;
    case SVC_P_MODE:                                        // состояние -> итог (0 - сделано); пока только AY
        o[r++] = n >= 1 && d[0] == SVC_CHIP_AY && s->mode_ay ? s->mode_ay(s->user) : 0xFF;
        break;
    default:                                                // CFG_* и прочее - пока нет: ответ FF
        o[r++] = 0xFF;
        break;
    }
    s->reply[0] = (uint8_t)r, s->reply[1] = (uint8_t)(r >> 8);
    return (uint16_t)(r + 2);
}
