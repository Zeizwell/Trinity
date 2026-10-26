// Конвертер Bitphase -> FT2 (btp2ft2.h).
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "btp2ft2.h"
#include "../third_party/uzlib/uzlib.h"

// ---- раскладка FT2 (FT2 sdk/plugin.asm: CH_*, CHIP_*, IR_*, CMD_*)
#define CH_NOTE 0
#define CH_SAMPLE 1
#define CH_ENVSHAPE 2
#define CH_ORNAMENT 3
#define CH_VOLUME 4
#define CH_CMD 6
#define CHIP_ENVPER 0
#define CHIP_NOISE 4
#define CHIP_ECMD 2                 // команда огибающей чипа (FT2 sdk: CHIP_ECMD)
#define NOTE_OFF 97
#define CMD_EXT 0xF

// ---- лог: одна строка на вид потери (номер вида - warned[])
enum { W_TS, W_RELEASE, W_INSNUM, W_TABLE, W_EFF2, W_EFFUNK, W_EFFQ, W_EFFE, W_EFFTAB, W_DELAY, W_ENVEFF, W_PATS,
       W_ORDER, W_INSLOOP, W_INSLEN, W_OLDINS, W_STEREO, W_ORNLEN, W_NOTE, W_TIMER, W_DUP,
       W_TABLEN, W_TABVAL, W_TABS, W_SIDSYNC, W_DETUNE, W_SWEEP, W_ENVSLOT, W_TUNE };

static void logf_(btp_conv_t *c, const char *t) { if (c->log) c->log(c->user, t); }

static void warn(btp_conv_t *c, int kind, const char *t) {
    if (c->warned[kind]) return;
    c->warned[kind] = true;
    logf_(c, t);
}

// ================= выход: файл FT2C, блоки «тег, длина, данные», CRC-16 CCITT =================

static uint8_t blk[8192];
static uint32_t blk_n;

static void out(btp_conv_t *c, const uint8_t *p, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {                      // CRC (#1021, с #FFFF) - всего до тега END
        c->crc ^= (uint16_t)p[i] << 8;
        for (int k = 0; k < 8; k++) c->crc = c->crc & 0x8000 ? (uint16_t)(c->crc << 1 ^ 0x1021) : (uint16_t)(c->crc << 1);
    }
    c->out(c->user, p, n);
}

static void put(uint8_t b) { if (blk_n < sizeof blk) blk[blk_n++] = b; }
static void put_str(const char *s, int max) { int n = (int)strlen(s); if (n > max - 1) n = max - 1; for (int i = 0; i < n; i++) put((uint8_t)s[i]); put(0); }

// Блок целиком: тег, длина, данные, чётность
static void block(btp_conv_t *c, const char *tag) {
    uint8_t h[6] = { (uint8_t)tag[0], (uint8_t)tag[1], (uint8_t)tag[2], (uint8_t)tag[3], (uint8_t)blk_n, (uint8_t)(blk_n >> 8) };
    out(c, h, 6);
    out(c, blk, blk_n);
    if (blk_n & 1) { uint8_t z = 0; out(c, &z, 1); }
    blk_n = 0;
}

// ================= проход 1 =================

// Макросы инструмента: 0..11 - обычные (macros), 12..22 - таймерные (timerMacros)
static const char *const mac_name[] = { "volume", "amplitudeSlide", "tone", "toneAdd", "toneAccumulation", "noise",
    "noiseAdd", "noiseAccumulation", "envelope", "envelopeAdd", "envelopeAccumulation", "retriggerEnvelope",
    "sid", "syncbuzzer", "fm", "fmOffsetMode", "fmWaveform", "envFm", "envFmOffsetMode", "envFmWaveform",
    "timerWaveform", "semitone", "detune" };
#define MAC_N 12                    // обычных
#define MAC_ALL 23
enum { T_SID = 12, T_SYNC, T_FM, T_FMMODE, T_FMWAVE, T_EFM, T_EFMMODE, T_EFMWAVE, T_WAVE, T_SEMI, T_DET };

static int mac_find(const char *k, int from, int to) {
    for (int i = from; i < to; i++) if (!strcmp(mac_name[i], k)) return i;
    return -1;
}

// Значение макроса на тике t: до конца - по порядку, дальше - по кругу с цикла
static int mac_at(const btp_conv_t *c, int m, int t) {
    int n = c->mac_n[m], l = c->mac_loop[m];
    if (!n) return 0;
    if (t >= n) { if (l >= n) l = n - 1; t = l + (t - l) % (n - l); }
    return c->mac_v[m][t];
}

static int gcd(int a, int b) { while (b) { int t = a % b; a = b, b = t; } return a; }

// Макросы m0..m1-1 (у каждого своя длина и цикл) -> общие строки: цикл S = самый поздний цикл, период = НОК петель.
// Не влезает в max строк - режем, лог. -> строк, *loop - цикл
static int span(btp_conv_t *c, int m0, int m1, int max, int *loop, const char *what, int id) {
    int s = 0, p = 1, len = 1;
    for (int m = m0; m < m1; m++) {
        int n = c->mac_n[m], l = c->mac_loop[m];
        if (!n) continue;
        if (l >= n) l = n - 1;
        if (n > len) len = n;
        if (l > s) s = l;
        int q = n - l;
        p = p / gcd(p, q) * q;
        if (p > max) p = max;
    }
    int rows = s + p > len ? s + p : len;
    if (rows > max) {
        char t[64];
        snprintf(t, sizeof t, "instrument %02d: %s loops differ - cut to %d rows", id, what, max);
        logf_(c, t);
        rows = max;
        if (s > max - 1) s = max - 1;
    }
    *loop = s;
    return rows;
}

static void ins_make(btp_conv_t *c, int id) {
    btp_ins_t *in = &c->ins[id];
    int s, rows = span(c, 0, MAC_N, 64, &s, "macro", id);
    in->rows = (uint8_t)rows, in->loop = (uint8_t)s;
    for (int t = 0; t < rows; t++) {
        uint8_t *r = in->row[t];
        int tone = mac_at(c, 3, t) & 0xFFF, slide = mac_at(c, 1, t), env = -mac_at(c, 9, t);   // наша огибающая - вычитается
        r[0] = (uint8_t)((mac_at(c, 6, t) & 0x1F) | (mac_at(c, 5, t) ? 0 : 0x40) | (mac_at(c, 7, t) ? 0x80 : 0));
        r[1] = (uint8_t)tone;
        r[2] = (uint8_t)((tone >> 8) | (mac_at(c, 2, t) ? 0 : 0x40) | (mac_at(c, 4, t) ? 0x80 : 0) | (mac_at(c, 11, t) ? 0x10 : 0));
        r[3] = (uint8_t)((mac_at(c, 0, t) & 15) | (slide ? 0x20 : 0) | (slide < 0 ? 0x10 : 0) |
                         (mac_at(c, 8, t) ? 0x40 : 0) | (mac_at(c, 10, t) ? 0x80 : 0));
        r[4] = (uint8_t)(env < -128 ? -128 : env > 127 ? 127 : env);
    }
}

// ---- FX: таблицы и строки

// Волна строкой "[15,0,...]" -> номер таблицы FX (такая уже есть - её; 0 - пресет «меандр SID F 0»; свои - с 6)
static int tab_get(btp_conv_t *c, const char *s) {
    uint8_t v[16];
    int n = 0;
    for (const char *p = s; *p; ) {
        while (*p && *p != '-' && (*p < '0' || *p > '9')) p++;
        if (!*p) break;
        long x = strtol(p, (char **)&p, 10);
        if (n == 16) { warn(c, W_TABLEN, "timer wave longer than 16 steps - cut"); break; }
        if (x < -128 || x > 127) warn(c, W_TABVAL, "timer wave value out of -128..127 - clamped");
        v[n++] = (uint8_t)(x < -128 ? -128 : x > 127 ? 127 : x);
    }
    if (!n) v[n++] = 0;
    for (int t = 0; t < c->tabs; t++)
        if ((t == 0 || t >= 6) && c->tab_n[t] == n && !memcmp(c->tab[t], v, n)) return t;
    if (c->tabs > 255) { warn(c, W_TABS, "timer waves over 250 - lost"); return 0; }
    memcpy(c->tab[c->tabs], v, n);
    c->tab_n[c->tabs] = (uint8_t)n;
    return c->tabs++;
}

// Скважность Bitphase 0..100 % -> наша ступень 0..15 (0..50 %; d и 100 - d звучат одинаково)
static uint8_t duty(int d) {
    if (d > 50) d = 100 - d;
    if (d < 0) d = 0;
    return (uint8_t)((d * 15 + 25) / 50);
}

// Таймерные макросы и ШИМ инструмента -> FX с тем же номером (строки по тикам, как у инструмента; до 32)
static void fx_make(btp_conv_t *c, int id) {
    btp_fx_t *f = &c->fx[id];
    int s, rows = span(c, T_SID, MAC_ALL, 32, &s, "timer macro", id), any = 0;
    for (int t = 0; t < rows; t++) {
        uint8_t *r = f->row[t];
        int sid = mac_at(c, T_SID, t), sync = mac_at(c, T_SYNC, t), fm = mac_at(c, T_FM, t), efm = mac_at(c, T_EFM, t);
        if (sid && sync) warn(c, W_SIDSYNC, "SID and Sync at once - SID taken");
        any |= sid | sync | fm | efm;
        r[0] = (uint8_t)((sid ? 1 : sync ? 2 : 0) | (fm ? 4 : 0) | (efm ? 8 : 0) |
                         (mac_at(c, T_FMMODE, t) ? 0x20 : 0) | (mac_at(c, T_EFMMODE, t) ? 0x40 : 0));
        r[1] = (uint8_t)(c->pwm_max << 4 | c->pwm_min);
        r[2] = c->pwm_speed;
        int semi = mac_at(c, T_SEMI, t), det = mac_at(c, T_DET, t);
        if (det < -128 || det > 127) warn(c, W_DETUNE, "timer detune out of -128..127 - clamped");
        uint8_t sb = (uint8_t)semi, db = (uint8_t)(det < -128 ? -128 : det > 127 ? 127 : det);
        int tw = mac_at(c, T_WAVE, t), tf = mac_at(c, T_FMWAVE, t), te = mac_at(c, T_EFMWAVE, t);
        r[3] = sb, r[4] = db, r[5] = (uint8_t)tw;           // Mix: полутоны, сырой детюн, таблица - у всех трёх общие
        r[6] = sb, r[7] = db, r[8] = (uint8_t)tf;
        r[9] = sb, r[10] = db, r[11] = (uint8_t)te;
        r[12] = (uint8_t)((c->tab_n[tw] - 1) | (c->tab_n[tf] - 1) << 4);
        r[13] = (uint8_t)(c->tab_n[te] - 1);
    }
    if (!any) return;                                       // эффекты ни на одном тике не включены - FX нет
    f->rows = (uint8_t)rows, f->loop = (uint8_t)s;
    f->shape = c->pwm_shape, f->phase = c->pwm_phase, f->keep = c->pwm_keep;
}

// Номер инструмента Bitphase: строка "01".."0W" - число в 36-ричной
static int ins_id(const char *s) { return (int)strtol(s, NULL, 36); }

static void pass1(btp_conv_t *c, int type) {
    json_t *j = &c->js;
    int d = j->depth;
    if (d == 1 && type == JSON_STRING) {                    // имя, автор
        if (json_is(j, 0, "name")) strncpy(c->name, j->str, 32);
        if (json_is(j, 0, "author")) strncpy(c->author, j->str, 32);
        return;
    }
    if (d == 1 && type == JSON_NUM) {
        if (json_is(j, 0, "initialSpeed")) c->speed = (uint8_t)j->num;
        if (json_is(j, 0, "loopPointId")) c->loop = (uint16_t)j->num;
        return;
    }
    if (d == 2 && type == JSON_NUM && json_is(j, 0, "patternOrder")) {
        if (c->order_n < BTP_ORDER) c->order[c->order_n++] = (uint8_t)j->num;
        return;
    }
    if (json_is(j, 0, "songs")) {                           // песня: чип, частота, стерео, длины паттернов
        if (d == 2 && type == JSON_END_OBJ) c->songs++;
        if (json_idx(j, 1) != 0) return;
        // Tune - по самой таблице нот песни (Bitphase берёт её отдельно от частоты чипа): C-1 3344 - наша 1.75 МГц,
        // 3389 - 1.7734 (FT2 srs/player.asm Note_175 / Note_177); другая - ближайшая, в лог
        if (d == 4 && type == JSON_NUM && json_is(j, 2, "tuningTable") && json_idx(j, 3) == 0) {
            int p = (int)j->num;
            c->tune = p >= (3344 + 3389) / 2;
            if (p != 3344 && p != 3389) warn(c, W_TUNE, "note table not 1.75 / 1.7734 MHz - nearest taken");
        }
        if (d == 3 && type == JSON_STRING && json_is(j, 2, "chipVariant")) c->chip = !strcmp(j->str, "YM");
        if (d == 3 && type == JSON_STRING && json_is(j, 2, "stereoLayout")) {
            static const char *const st[5] = { "ABC", "ACB", "BAC", "BCA", "CAB" };
            c->stereo = 0;
            for (int i = 0; i < 5; i++) if (!strcmp(j->str, st[i])) c->stereo = (uint8_t)i;
            if (strcmp(j->str, st[c->stereo])) warn(c, W_STEREO, "stereo: not ABC / ACB / BAC / BCA / CAB - set ABC");
        }
        if (d == 5 && type == JSON_NUM && json_is(j, 2, "patterns")) {
            if (json_is(j, 4, "id")) c->cur_id = (int)j->num;
            if (json_is(j, 4, "length") && c->cur_id >= 0 && c->cur_id < BTP_SRC_PATS && !c->src_len[c->cur_id])
                c->src_len[c->cur_id] = (uint16_t)j->num;   // номер дважды - длина первого
        }
        return;
    }
    if (json_is(j, 0, "tables")) {                          // таблица -> орнамент (номер таблицы + 1)
        if (d == 2 && type == JSON_BEGIN_OBJ) c->cur_id = -1, c->cur_len = 0;
        if (d == 3 && type == JSON_NUM && json_is(j, 2, "id")) c->cur_id = (int)j->num + 1;
        btp_orn_t *o = c->cur_id >= 1 && c->cur_id <= BTP_ORN ? &c->orn[c->cur_id] : NULL;
        if (!o) { if (d == 3 && json_is(j, 2, "id") && c->cur_id > BTP_ORN) logf_(c, "table over 16 - lost"); return; }
        if (d == 3 && type == JSON_NUM && json_is(j, 2, "loop")) o->loop = (uint8_t)j->num;
        if (d == 3 && type == JSON_STRING && json_is(j, 2, "name")) strncpy(o->name, j->str, 16);
        if (d == 3 && type == JSON_TRUE && json_is(j, 2, "additive"))
            for (int i = 0; i < 64; i++) o->row[i][0] |= 0x40;      // нота накоплением
        if (d == 4 && type == JSON_NUM && json_is(j, 2, "rows")) {
            if (o->rows < 64) o->row[o->rows][1] = (uint8_t)(int)j->num, o->rows++;
            else warn(c, W_ORNLEN, "table longer than 64 - cut");
        }
        return;
    }
    if (json_is(j, 0, "instruments")) {                     // инструмент: макросы -> строки
        if (d == 2 && type == JSON_BEGIN_OBJ) {
            c->cur_id = 0;
            memset(c->mac_n, 0, sizeof c->mac_n), memset(c->mac_loop, 0, sizeof c->mac_loop);
            c->pwm_max = duty(50), c->pwm_min = 0, c->pwm_speed = 0, c->pwm_shape = 1, c->pwm_phase = 0, c->pwm_keep = 0;
        }
        if (d == 3 && type == JSON_STRING && json_is(j, 2, "id")) {
            c->cur_id = ins_id(j->str);
            if (c->cur_id < 1 || c->cur_id > BTP_INS) {
                char t[48];
                snprintf(t, sizeof t, "instrument %s: number over 32 - lost", j->str);
                logf_(c, t);
                c->cur_id = 0;
            }
        }
        if (!c->cur_id) return;
        btp_ins_t *in = &c->ins[c->cur_id];
        if (d == 3 && type == JSON_STRING && json_is(j, 2, "name")) strncpy(in->name, j->str, 16);
        if (d == 3 && type == JSON_BEGIN_ARR && json_is(j, 2, "rows"))
            warn(c, W_OLDINS, "old-format instruments (rows) - not converted yet");
        bool tm = json_is(j, 2, "timerMacros");
        if ((json_is(j, 2, "macros") || tm) && d >= 5) {    // макрос: цикл, значения (волна - сразу номер таблицы)
            int m = tm ? mac_find(json_key(j, 3), MAC_N, MAC_ALL) : mac_find(json_key(j, 3), 0, MAC_N);
            if (m < 0) return;
            if (d == 5 && type == JSON_NUM && json_is(j, 4, "loop")) c->mac_loop[m] = (uint8_t)j->num;
            if (d == 6 && json_is(j, 4, "values") && c->mac_n[m] < 64)
                c->mac_v[m][c->mac_n[m]++] = (int16_t)(type == JSON_NUM ? (int)j->num : type == JSON_STRING ? tab_get(c, j->str)
                                                       : type == JSON_TRUE);
        }
        if (d == 3 && type == JSON_NUM) {                   // ШИМ инструмента -> наши единицы (doc/bitphase-fx.md п.4-5)
            int v = (int)j->num;
            if (json_is(j, 2, "timerPwmDuty")) c->pwm_max = duty(v);
            if (json_is(j, 2, "timerPwmSweepMin")) c->pwm_min = duty(v);
            if (json_is(j, 2, "timerPwmSweep")) {            // фаза 0..1000 -> 0..255
                c->pwm_speed = (uint8_t)((v * 256 + 500) / 1001);
                if (v && !c->pwm_speed) c->pwm_speed = 1, warn(c, W_SWEEP, "PWM sweep slower than ours - set slowest");
            }
            if (json_is(j, 2, "timerPwmSweepStartPhase")) c->pwm_phase = (uint8_t)(v * 256 / 1001);
        }
        if (d == 3 && type == JSON_STRING && json_is(j, 2, "timerPwmSweepShape")) {   // форма -> таблица-пресет 1..5
            static const char *const sh[5] = { "triangle", "sine", "sawUp", "sawDown", "square" };   // как у Bitphase
            c->pwm_shape = 1;
            for (int i = 0; i < 5; i++) if (!strcmp(j->str, sh[i])) c->pwm_shape = (uint8_t)(i + 1);
        }
        if (d == 3 && (type == JSON_TRUE || type == JSON_FALSE) && json_is(j, 2, "timerPwmPreserveOnNewNote"))
            c->pwm_keep = type == JSON_TRUE;
        if (d == 2 && type == JSON_END_OBJ) ins_make(c, c->cur_id), fx_make(c, c->cur_id);
    }
}

// ================= проход 2: паттерны =================

// Команда Bitphase -> наша (2 байта); 0 - не легла
static int eff_map(btp_conv_t *c, int e, int delay, int par, uint8_t *o) {
    if (delay > 15) { warn(c, W_DELAY, "effect delay over 15 - set 15"); delay = 15; }
    int code;
    switch (e) {
    case 1: code = 1; break;                                // слайд (как VT 1 / 2 - проверить на слух)
    case 2: code = 2; break;
    case 4: code = 4; break;                                // сэмпл с позиции
    case 5: code = 5; break;                                // орнамент с позиции
    case 6: code = 6; break;                                // вкл / выкл
    case 7: o[0] = CMD_EXT << 4 | 2, o[1] = (uint8_t)par; return 1;   // задержка ноты - F2
    case 8: code = 7; break;                                // слайд громкости
    case 'A': code = 0xC; break;                            // арпеджио
    case 'P': code = 3; break;                              // портаменто
    case 'S': code = 0xB; delay = 0; break;                 // скорость
    case 'V': code = 8; break;                              // вибрато
    case 'D': o[0] = CMD_EXT << 4 | 1, o[1] = (uint8_t)(par - 0x80); return 1;   // детюн: 80 = 0 -> со знаком
    case 'Q': warn(c, W_EFFQ, "effect Q (end of song) - lost"); return 0;
    case 'E': warn(c, W_EFFE, "effects E (PWM from the pattern) - lost (not yet in FT2)"); return 0;
    default: {
        char t[48];
        snprintf(t, sizeof t, "effect %d - unknown, lost", e);
        warn(c, W_EFFUNK, t);
        return 0;
    }
    }
    o[0] = (uint8_t)(code << 4 | delay), o[1] = (uint8_t)par;
    return 1;
}

static int eff_e, eff_d, eff_p, eff_tab, note_n, note_o;

static void pass2(btp_conv_t *c, int type) {
    json_t *j = &c->js;
    int d = j->depth;
    if (!json_is(j, 0, "songs") || json_idx(j, 1) != 0 || !json_is(j, 2, "patterns")) return;
    if (d == 4 && type == JSON_BEGIN_OBJ) { c->cur_id = -1; memset(c->row, 0, sizeof c->row); return; }
    if (d == 5 && type == JSON_NUM && json_is(j, 4, "id")) { c->cur_id = (int)j->num; return; }
    if (d == 4 && type == JSON_END_OBJ) {                   // паттерн разобран: куски по 64 роу -> PATT
        if (c->cur_id < 0 || c->cur_id >= BTP_SRC_PATS || c->first[c->cur_id] == 0xFF) return;
        if (c->seen[c->cur_id]++) { warn(c, W_DUP, "pattern number used twice in the file - first taken"); return; }
        int len = c->src_len[c->cur_id] ? c->src_len[c->cur_id] : 64;
        for (int k = 0; k * 64 < len; k++) {
            int rows = len - k * 64 > 64 ? 64 : len - k * 64, skip = 0;
            put((uint8_t)(c->first[c->cur_id] + k)), put((uint8_t)rows);
            for (int r = 0; r < rows; r++) {                // роу: пропуск, маска кусков, по куску - маска байт, байты
                const uint8_t *rw = c->row[k * 64 + r];
                uint8_t mask = 0;
                for (int s = 0; s < 4; s++) {
                    int off = s ? 8 + (s - 1) * 16 : 0, n = s ? 16 : 8;
                    for (int b = 0; b < n; b++) if (rw[off + b]) { mask |= (uint8_t)(1 << s); break; }
                }
                if (!mask) { skip++; continue; }
                put((uint8_t)skip), put(mask);
                skip = 0;
                for (int s = 0; s < 4; s++) {
                    if (!(mask & 1 << s)) continue;
                    int off = s ? 8 + (s - 1) * 16 : 0, n = s ? 16 : 8;
                    uint16_t bm = 0;
                    for (int b = 0; b < n; b++) if (rw[off + b]) bm |= (uint16_t)(1 << b);
                    put((uint8_t)bm);
                    if (n == 16) put((uint8_t)(bm >> 8));
                    for (int b = 0; b < n; b++) if (rw[off + b]) put(rw[off + b]);
                }
            }
            block(c, "PATT");
        }
        return;
    }
    // общая колонка: период огибающей, шум
    if (json_is(j, 4, "patternRows") && d == 7 && type == JSON_NUM) {
        int r = json_idx(j, 5), v = (int)j->num;
        if (r < 0 || r > 255 || !v) return;
        if (json_is(j, 6, "envelopeValue")) c->row[r][CHIP_ENVPER] = (uint8_t)v, c->row[r][CHIP_ENVPER + 1] = (uint8_t)(v >> 8);
        if (json_is(j, 6, "noiseValue")) c->row[r][CHIP_NOISE] = (uint8_t)(v & 31 ? v & 31 : 0x20);
        return;
    }
    // эффект общей колонки -> команда огибающей чипа (CHIP_ECMD, FT2 doc/commands.md «Колонка огибающей»): те же коды,
    // что у канала, цель - период огибающей. Плеер FT2 пока играет слайды 1 / 2; EA (автоогибающая) - в лог
    if (json_is(j, 4, "patternRows") && json_is(j, 6, "envelopeEffect")) {
        if (d == 7 && type == JSON_BEGIN_OBJ) eff_e = eff_d = eff_p = 0, eff_tab = -1;
        if (d == 8 && type == JSON_NUM) {
            if (json_is(j, 7, "effect")) eff_e = (int)j->num;
            if (json_is(j, 7, "delay")) eff_d = (int)j->num;
            if (json_is(j, 7, "parameter")) eff_p = (int)j->num;
            if (json_is(j, 7, "tableIndex")) eff_tab = (int)j->num;
        }
        if (d == 7 && type == JSON_END_OBJ) {
            int r = json_idx(j, 5);
            if (r < 0 || r > 255) return;
            if (eff_tab >= 0) { warn(c, W_EFFTAB, "effects with a table (XTY) - lost"); return; }
            if (eff_e == 'E') { warn(c, W_ENVEFF, "envelope column: EA (auto-envelope) - lost (not in FT2 yet)"); return; }
            if (eff_e != 1 && eff_e != 2) warn(c, W_ENVSLOT, "envelope column: effects other than slides - written, FT2 player does not play them yet");
            eff_map(c, eff_e, eff_d, eff_p, c->row[r] + CHIP_ECMD);
        }
        return;
    }
    if (!json_is(j, 4, "channels") || d < 8) return;
    int ch = json_idx(j, 5), r = json_idx(j, 7);
    if (ch < 0 || ch > 2 || r < 0 || r > 255) return;
    uint8_t *cell = c->row[r] + 8 + ch * 16;
    if (d == 8 && type == JSON_BEGIN_OBJ) { c->eff_n = 0; return; }
    if (d == 9 && type == JSON_NUM) {
        int v = (int)j->num;
        if (json_is(j, 8, "instrument") && v) {
            if (v <= BTP_INS) cell[CH_SAMPLE] = (uint8_t)v; else warn(c, W_INSNUM, "instrument number over 32 - lost");
        }
        if (json_is(j, 8, "envelopeShape") && v) cell[CH_ENVSHAPE] = (uint8_t)v;
        if (json_is(j, 8, "volume") && v) cell[CH_VOLUME] = (uint8_t)v;
        if (json_is(j, 8, "table") && v) {                  // 0 - пусто, -1 - выключить, k - таблица k-1 (орнамент k)
            if (v < 0) cell[CH_ORNAMENT] = 1;
            else if (v <= BTP_ORN) cell[CH_ORNAMENT] = (uint8_t)(v + 1);
            else warn(c, W_TABLE, "table over 16 in the pattern - lost");
        }
        return;
    }
    if (json_is(j, 8, "note")) {                            // нота: имя, октава -> номер (C-1 = 1)
        if (d == 9 && type == JSON_BEGIN_OBJ) note_n = note_o = 0;
        if (d == 10 && type == JSON_NUM) { if (json_is(j, 9, "name")) note_n = (int)j->num; else note_o = (int)j->num; }
        if (d == 9 && type == JSON_END_OBJ && note_n) {
            if (note_n == 1) cell[CH_NOTE] = NOTE_OFF;
            else if (note_n == 14) { cell[CH_NOTE] = NOTE_OFF; warn(c, W_RELEASE, "note Release - set Off"); }
            else {
                int n = (note_o - 1) * 12 + (note_n - 2) + 1;
                if (n >= 1 && n <= 96) cell[CH_NOTE] = (uint8_t)n; else warn(c, W_NOTE, "note out of C-1..B-8 - lost");
            }
        }
        return;
    }
    if (json_is(j, 8, "effects")) {                         // команда: собрать поля, в конце объекта - в клетку
        if (d == 10 && type == JSON_BEGIN_OBJ) eff_e = eff_d = eff_p = 0, eff_tab = -1;
        if (d == 11 && type == JSON_NUM) {
            if (json_is(j, 10, "effect")) eff_e = (int)j->num;
            if (json_is(j, 10, "delay")) eff_d = (int)j->num;
            if (json_is(j, 10, "parameter")) eff_p = (int)j->num;
            if (json_is(j, 10, "tableIndex")) eff_tab = (int)j->num;
        }
        if (d == 10 && type == JSON_END_OBJ) {
            if (eff_tab >= 0) { warn(c, W_EFFTAB, "effects with a table (XTY) - lost"); return; }
            if (c->eff_n >= 2) { warn(c, W_EFF2, "more than 2 effects in a cell - lost"); return; }
            if (eff_map(c, eff_e, eff_d, eff_p, cell + CH_CMD + c->eff_n * 2)) c->eff_n++;
        }
    }
}

static void on_json(json_t *j, int type) {
    btp_conv_t *c = (btp_conv_t *)j->user;
    if (c->pass == 1) pass1(c, type); else pass2(c, type);
}

// ================= проходы =================

void btp_init(btp_conv_t *c, btp_out_fn outf, btp_log_fn log, void *user) {
    memset(c, 0, sizeof *c);
    c->out = outf, c->log = log, c->user = user;
    c->speed = 3;
    c->tab[0][0] = 15, c->tab_n[0] = 2;                    // таблица 0 - пресет «меандр SID F 0» (как у нового трека)
    c->tabs = 6;                                            // 1..5 - пресеты форм свипа, свои - с 6
    c->crc = 0xFFFF;                                        // CRC-16 CCITT - с #FFFF (как FT2 Cn_Crc)
}

// Между проходами: ордер FT2 (паттерны Bitphase по первому появлению, куски по 64 роу), заголовок, ордер в файл
static void pass1_end(btp_conv_t *c) {
    memset(c->first, 0xFF, sizeof c->first);
    uint8_t ord[256];
    int on = 0, loop = 0;
    for (int i = 0; i < c->order_n; i++) {
        int id = c->order[i], len = c->src_len[id] ? c->src_len[id] : 64, k = (len + 63) / 64;
        if (c->first[id] == 0xFF) {
            if (c->pats + k > BTP_PATS) { warn(c, W_PATS, "patterns over 64 - lost (order shortened)"); continue; }
            c->first[id] = c->pats, c->pats = (uint8_t)(c->pats + k);
            if (len > 64) {
                char t[64];
                snprintf(t, sizeof t, "pattern %d: %d rows - split into %d", id, len, k);
                logf_(c, t);
            }
        }
        if (i == c->loop) loop = on;
        for (int q = 0; q < k; q++) {
            if (on >= 256) { warn(c, W_ORDER, "order over 256 positions - cut"); break; }
            ord[on++] = (uint8_t)(c->first[id] + q);
        }
    }
    if (c->songs > 1) warn(c, W_TS, "TurboSound (2 songs): second chip not converted yet");
    static const uint8_t hdr[6] = { 'F', 'T', '2', 'C', 1, 0 };
    out(c, hdr, 6);
    int ni = 0, no = 0, nf = 0;
    for (int i = 1; i <= BTP_INS; i++) ni += c->ins[i].rows != 0, nf += c->fx[i].rows != 0;
    for (int i = 1; i <= BTP_ORN; i++) no += c->orn[i].rows != 0;
    put(1), put(c->speed), put(c->tune), put(c->chip), put(c->stereo), put(1), put(nf ? 1 : 0);   // чипов, скорость, Tune,
    put(c->pats), put((uint8_t)ni), put((uint8_t)no), put((uint8_t)nf), put((uint8_t)(c->tabs - 6));   //   тип, стерео, PT, FX
    put_str(c->name, 33), put_str(c->author, 33);
    block(c, "HEAD");
    put((uint8_t)(on ? on - 1 : 0)), put((uint8_t)loop);
    for (int i = 0; i < on; i++) put(ord[i]);
    block(c, "ORDR");
}

void btp_pass(btp_conv_t *c, int pass) {
    if (pass == 2) pass1_end(c);
    c->pass = pass;
    c->cur_id = -1;
    json_init(&c->js, on_json, c);
}

void btp_feed(btp_conv_t *c, const uint8_t *p, uint32_t n) { json_feed(&c->js, p, n); }

bool btp_finish(btp_conv_t *c) {
    for (int i = 1; i <= BTP_INS; i++) {                    // инструменты
        btp_ins_t *in = &c->ins[i];
        if (!in->rows) continue;
        put((uint8_t)i), put(in->loop), put((uint8_t)(in->rows - 1));
        for (int r = 0; r < in->rows; r++) for (int b = 0; b < 5; b++) put(in->row[r][b]);
        put(c->fx[i].rows ? (uint8_t)i : 0);                // FX инструмента - тот же номер
        put_str(in->name, 17);
        block(c, "INST");
    }
    for (int i = 1; i <= BTP_INS; i++) {                    // FX: номер, заголовок 5 байт, строки, имя
        btp_fx_t *f = &c->fx[i];
        if (!f->rows) continue;
        put((uint8_t)i), put((uint8_t)(f->rows - 1)), put(f->loop), put(f->shape), put(f->phase), put(f->keep);
        for (int r = 0; r < f->rows; r++) for (int b = 0; b < 16; b++) put(f->row[r][b]);
        put_str(c->ins[i].name, 17);
        block(c, "FX16");
    }
    for (int t = 6; t < c->tabs; t++) {                     // таблицы FX: номер, 16 значений
        put((uint8_t)t);
        for (int k = 0; k < 16; k++) put(c->tab[t][k]);
        block(c, "TB16");
    }
    for (int i = 1; i <= BTP_ORN; i++) {                    // орнаменты
        btp_orn_t *o = &c->orn[i];
        if (!o->rows) continue;
        put((uint8_t)i), put(o->loop < o->rows ? o->loop : 0), put((uint8_t)(o->rows - 1));
        for (int r = 0; r < o->rows; r++) put(o->row[r][0]), put(o->row[r][1]);
        put_str(o->name, 17);
        block(c, "ORNM");
    }
    uint8_t t[8] = { 'E', 'N', 'D', ' ', 2, 0, (uint8_t)c->crc, (uint8_t)(c->crc >> 8) };   // CRC - всего до тега END
    c->out(c->user, t, 8);
    char s[64];
    snprintf(s, sizeof s, "done: %d patterns, order %d", c->pats, c->order_n);
    logf_(c, s);
    return !c->js.error;
}

// ================= распаковка gzip потоком (uzlib, окно 32К) =================

bool btp_gunzip(const uint8_t *src, uint32_t len, btp_chunk_fn fn, void *user) {
    static uint8_t dict[32768], buf[4096];
    struct uzlib_uncomp d;
    uzlib_init();
    uzlib_uncompress_init(&d, dict, sizeof dict);
    d.source = src, d.source_limit = src + len, d.source_read_cb = NULL;
    if (uzlib_gzip_parse_header(&d) != TINF_OK) return false;
    for (;;) {
        d.dest_start = d.dest = buf, d.dest_limit = buf + sizeof buf;
        int r = uzlib_uncompress(&d);
        if (d.dest > buf) fn(user, buf, (uint32_t)(d.dest - buf));
        if (r == TINF_DONE) return true;
        if (r != TINF_OK) return false;
    }
}
