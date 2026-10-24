// Движок AY FX (ayfx.h). Логика - как в Unreal (sndchip.cpp, trin_*); вместо правки микшера - записи в AY.
#include "ayfx.h"
#include <string.h>
#include <math.h>

enum { TF_MIX = 3, TF_SID = 1, TF_SYNC = 2, TF_FM = 4, TF_EFM = 8, TF_FMPER = 32, TF_EFMPER = 64 };
static const uint8_t tf_bit[3] = { TF_MIX, TF_FM, TF_EFM };

// 2^(-s/12) для полутонов s = -128..127 (индекс - s как байт)
static float semi_mul[256];
static bool  semi_ok;

static float semi(uint8_t s) {
    if (!semi_ok) {
        for (int i = 0; i < 256; i++) semi_mul[i] = powf(2.0f, -(float)(int8_t)i / 12.0f);
        semi_ok = true;
    }
    return semi_mul[s];
}

static uint32_t clampf(float p, uint32_t max) {
    if (p < 1.0f) return 1;
    if (p > (float)max) return max;
    return (uint32_t)(p + 0.5f);
}

// ---- что сейчас в AY от Спектрума (тень)
static uint32_t tone(const ayfx_t *a, int c) {
    uint32_t p = a->regs[2 * c] | ((a->regs[2 * c + 1] & 15) << 8);
    return p ? p : 1;
}
static uint32_t env_per(const ayfx_t *a) {
    uint32_t p = a->regs[11] | (a->regs[12] << 8);
    return p ? p : 1;
}

// Период таймера эффекта k канала c (такты тона): всегда за тоном, детюн полутонами и сырой
static uint32_t period(const ayfx_t *a, int c, int k) {
    const uint8_t *r = a->ch[c].row;
    float p = (float)tone(a, c) * semi(r[3 + 3 * k]) + (float)(int8_t)r[4 + 3 * k];
    return clampf(p, 0xFFFF);
}

// Длина эффекта k (1..16 шагов): Mix - LEN1 3..0, FM - LEN1 7..4, Env FM - LEN2 3..0
static uint8_t len(const ayfx_t *a, int c, int k) {
    const uint8_t *r = a->ch[c].row;
    return (uint8_t)((k == 0 ? r[12] & 15 : k == 1 ? r[12] >> 4 : r[13] & 15) + 1);
}

// Свип ШИМ 0..1: форма - таблица 16 точек кривой 0..255, между ними - линейно (по кругу)
static float sweep_pos(const ayfx_t *a, const ayfx_ch_t *ch) {
    const uint8_t *t = a->tab[ch->shape];
    unsigned i = ch->sweep >> 12, f = ch->sweep & 0x0FFF;
    float x = t[i], y = t[(i + 1) & 15];
    return (x + (y - x) * (float)f / 4096.0f) / 255.0f;
}

// Вернуть чипу регистры Спектрума (эффект выключился)
static void restore_vol(ayfx_t *a, int c)  { a->out(a->user, (uint8_t)(8 + c), a->regs[8 + c]); }
static void restore_tone(ayfx_t *a, int c) {
    a->out(a->user, (uint8_t)(2 * c), a->regs[2 * c]);
    a->out(a->user, (uint8_t)(2 * c + 1), a->regs[2 * c + 1]);
}
static void restore_env(ayfx_t *a) {
    a->out(a->user, 11, a->regs[11]);
    a->out(a->user, 12, a->regs[12]);
}

static bool fx_on(const ayfx_ch_t *ch, int k) { return (ch->row[0] & tf_bit[k]) != 0; }

// План таймера k канала c: длина, регистр, длины шагов и готовое значение на каждый шаг. Тяжёлое (float) - здесь,
// не в прерывании. Зовётся, когда что-то поменялось: ROW, TABLE, RESET, Спектрум записал тон / громкость / огибающую,
// свип ШИМ (кадр)
static void plan(ayfx_t *a, int c, int k) {
    ayfx_ch_t *ch = &a->ch[c];
    ayfx_tmr_t *t = &ch->t[k];
    uint8_t l = len(a, c, k);
    const uint8_t *tab = a->tab[ch->row[5 + 3 * k]];
    uint32_t p = period(a, c, k);
    t->len = l, t->d0 = t->d1 = p, t->pwm = 0, t->two = (uint8_t)(k != 0);
    if (t->step >= l) t->step = 0;
    if (k == 0) {
        if ((ch->row[0] & TF_MIX) == TF_SID) {              // SID: громкость = громкость ноты * уровень шага
            uint8_t base = a->regs[8 + c];
            t->reg = (uint8_t)(8 + c);
            for (int s = 0; s < l; s++) {
                uint8_t lvl = tab[s] & 15;
                t->val[s] = (base & 0x10) ? (lvl ? base : 0)     // огибающая: вкл / выкл
                                          : (uint8_t)(((base & 15) * lvl + 7) / 15);
            }
            if (l == 2 && ch->row[1]) {                     // ШИМ: 2 шага делят 2P по скважности
                float lo = (ch->row[1] & 15) / 30.0f, hi = (ch->row[1] >> 4) / 30.0f, d = hi;
                if (ch->row[2]) d = lo + (hi - lo) * sweep_pos(a, ch);
                uint32_t hp = clampf(2.0f * (float)p * d, 2 * p - 1);
                t->pwm = 1, t->d0 = hp, t->d1 = 2 * p - hp;
            }
        } else {                                            // Sync: перезапуск огибающей, шаг - форма (0 - текущая)
            t->reg = 13;
            for (int s = 0; s < l; s++) t->val[s] = (tab[s] & 15) ? (tab[s] & 15) : (a->regs[13] & 15);
        }
    } else if (k == 1) {                                    // FM: период тона канала
        uint32_t t0 = tone(a, c);
        t->reg = (uint8_t)(2 * c);
        for (int s = 0; s < l; s++)
            t->val[s] = (uint16_t)((ch->row[0] & TF_FMPER) ? clampf((float)t0 + (float)(int8_t)tab[s], 0x0FFF)
                                                           : clampf((float)t0 * semi(tab[s]), 0x0FFF));
    } else {                                                // Env FM: период огибающей
        uint32_t e0 = env_per(a);
        t->reg = 11;
        for (int s = 0; s < l; s++)
            t->val[s] = (uint16_t)((ch->row[0] & TF_EFMPER) ? clampf((float)e0 + (float)(int8_t)tab[s], 0xFFFF)
                                                            : clampf((float)e0 * semi(tab[s]), 0xFFFF));
    }
    t->dur = (t->pwm && t->step) ? t->d1 : t->d0;
}

// Текущий шаг - в AY (готовое значение) и длина этого шага. Лёгкое: только из плана
static void out_step(ayfx_t *a, ayfx_tmr_t *t) {
    uint16_t v = t->val[t->step];
    a->out(a->user, t->reg, (uint8_t)v);
    if (t->two) a->out(a->user, (uint8_t)(t->reg + 1), (uint8_t)(v >> 8));
    t->dur = (t->pwm && t->step) ? t->d1 : t->d0;
}

// Перепланировать включённые таймеры канала c (маска k: бит 0 - Mix, 1 - FM, 2 - Env FM); now - и сразу в AY
static void replan(ayfx_t *a, int c, int mask, bool now) {
    for (int k = 0; k < 3; k++)
        if ((mask >> k & 1) && fx_on(&a->ch[c], k)) {
            plan(a, c, k);
            if (now) out_step(a, &a->ch[c].t[k]);
        }
}

// ROW: новая строка FX канала c; выключенные эффекты отдают регистры Спектруму, планы - заново
static void row_set(ayfx_t *a, int c, const uint8_t *row, uint8_t shape, uint8_t start, uint8_t keep) {
    ayfx_ch_t *ch = &a->ch[c];
    uint8_t was = ch->row[0];
    memcpy(ch->row, row, AYFX_ROW);
    ch->shape = shape, ch->start = start, ch->keep = keep & 1;
    uint8_t now = ch->row[0];
    if ((was & TF_FM) && !(now & TF_FM)) restore_tone(a, c);
    if ((was & TF_EFM) && !(now & TF_EFM)) restore_env(a);
    if ((was & TF_MIX) == TF_SID && (now & TF_MIX) != TF_SID) restore_vol(a, c);
    for (int k = 0; k < 3; k++) {
        if (!(now & tf_bit[k])) continue;
        bool start_now = !(was & tf_bit[k]);                // только что включился - с первого шага и сразу в AY
        if (start_now) ch->t[k].step = 0, ch->t[k].cnt = 0;
        plan(a, c, k);
        if (start_now) out_step(a, &ch->t[k]);
    }
}

// ---- Шина: протокол #C0 / #C1
void ayfx_bus_reset(ayfx_bus_t *b) { memset(b, 0, sizeof *b); }

// Выбран регистр reg (запись в #FFFD): ключ "TRIN" - только подряд в #C0
void ayfx_bus_select(ayfx_bus_t *b, uint8_t reg) {
    if (reg != 0xC0) b->key = 0;
    b->sel = reg;
}

int ayfx_bus_write(ayfx_bus_t *b, uint8_t val) {
    static const char key[] = "TRIN";
    if (b->sel == 0xC0) {
        b->pos = 0;                                         // любая запись - разбор потока с начала
        if (val == 0 && b->key == 0) { b->on = 0, b->info = 0; return AYFX_EV_CLOSE; }
        if ((char)val == key[b->key]) b->key++;
        else b->key = (char)val == key[0];
        if (b->key == 4) { b->on = 1, b->key = 0; return AYFX_EV_OPEN; }
        return AYFX_EV_NONE;
    }
    if (b->sel != 0xC1) return AYFX_EV_REG;
    if (!b->on) return AYFX_EV_NONE;                        // поток закрыт - байт мимо
    if (b->pos == 0) {                                      // код пакета -> его длина
        static const uint8_t plen[5] = { 0, 19, 18, 2, 1 };
        if (val == 0 || val > AYFX_P_INFO) return AYFX_EV_NONE;   // неизвестный код - пропустить байт
        b->need = plen[val];
    }
    b->pkt[b->pos++] = val;
    if (b->pos < b->need) return AYFX_EV_NONE;
    b->pos = 0;
    if (b->pkt[0] == AYFX_P_INFO) { b->info = 1; return AYFX_EV_NONE; }   // INFO - ответ шины, звуку нечего
    return AYFX_EV_PACKET;
}

// Что ответить на чтение выбранного регистра (не сдвигая INFO). false - не наш (отвечает тень AY)
bool ayfx_bus_peek(const ayfx_bus_t *b, uint8_t *val) {
    if (!b->on) return false;
    if (b->sel == 0xC0) { *val = 'T'; return true; }
    if (b->sel == 0xC1) {
        static const uint8_t info[5] = { 'T', 'R', 'I', 'N', AYFX_VER };
        *val = (b->info >= 1 && b->info <= 5) ? info[b->info - 1] : 0;
        return true;
    }
    return false;
}

// Чтение было: ответ INFO - следующий байт
void ayfx_bus_read_done(ayfx_bus_t *b) {
    if (!b->on || b->sel != 0xC1) return;
    if (b->info >= 1 && b->info < 5) b->info++; else b->info = 0;
}

// ---- Звук: пакет от шины
void ayfx_packet(ayfx_t *a, const uint8_t *b) {
    switch (b[0]) {
    case AYFX_P_ROW:
        if (b[1] < 3) row_set(a, b[1], b + 2, b[16], b[17], b[18]);
        break;
    case AYFX_P_TABLE:
        memcpy(a->tab[b[1]], b + 2, AYFX_STEPS);
        for (int c = 0; c < 3; c++) replan(a, c, 7, false);    // таблица могла быть у кого-то в плане
        break;
    case AYFX_P_RESET:                                      // новая нота: таймеры, шаги, свип - с начала
        for (int c = 0; c < 3; c++) {
            if (!(b[1] & (1 << c))) continue;
            ayfx_ch_t *ch = &a->ch[c];
            if (!ch->keep) ch->sweep = (uint16_t)(ch->start << 8);
            for (int k = 0; k < 3; k++) ch->t[k].step = 0, ch->t[k].cnt = 0;
            replan(a, c, 7, true);
        }
        break;
    }
}

void ayfx_init(ayfx_t *a, ayfx_out_fn out, void *user) {
    a->out = out, a->user = user;
    ayfx_reset(a);
}

void ayfx_reset(ayfx_t *a) {
    memset(a->ch, 0, sizeof a->ch);
    memset(a->tab, 0, sizeof a->tab);
    memset(a->regs, 0, sizeof a->regs);
}

bool ayfx_reg_write(ayfx_t *a, uint8_t reg, uint8_t val) {
    if (reg > 15) return false;
    a->regs[reg] = val;
    bool own = false;                                       // регистр сейчас у эффекта - в AY пишет движок
    for (int c = 0; c < 3; c++) {                           // что зависит от регистра - план заново
        ayfx_ch_t *ch = &a->ch[c];
        if (reg == 8 + c && (ch->row[0] & TF_MIX) == TF_SID) { replan(a, c, 1, true); own = true; }
        if (reg == 2 * c || reg == 2 * c + 1) {             // тон канала: от него периоды всех таймеров канала
            replan(a, c, 1 | 4, false);
            if (fx_on(ch, 1)) { replan(a, c, 2, true); own = true; }
        }
        if ((reg == 11 || reg == 12) && fx_on(ch, 2)) { replan(a, c, 4, true); own = true; }
        if (reg == 13 && (ch->row[0] & TF_MIX) == TF_SYNC) replan(a, c, 1, false);   // форма по умолчанию у Sync
    }
    return own;
}

void ayfx_tick(ayfx_t *a, uint32_t n) {
    for (int c = 0; c < 3; c++) {
        ayfx_ch_t *ch = &a->ch[c];
        if (!(ch->row[0] & (TF_MIX | TF_FM | TF_EFM))) continue;
        for (int k = 0; k < 3; k++) {
            if (!fx_on(ch, k)) continue;
            ayfx_tmr_t *t = &ch->t[k];
            t->cnt += n;
            while (t->dur && t->cnt >= t->dur) {            // шаги, что прошли за n тактов: только готовое
                t->cnt -= t->dur;
                if (++t->step >= t->len) t->step = 0;
                out_step(a, t);
            }
        }
    }
}

void ayfx_frame(ayfx_t *a) {
    for (int c = 0; c < 3; c++)
        if (a->ch[c].row[2]) {
            a->ch[c].sweep = (uint16_t)(a->ch[c].sweep + a->ch[c].row[2] * 256);
            if (a->ch[c].t[0].pwm) replan(a, c, 1, false);      // скважность сдвинулась - длины шагов заново
        }
}

// Через сколько тактов тона ближайший шаг среди включённых таймеров (будильник ставится на него)
uint32_t ayfx_next(const ayfx_t *a) {
    uint32_t m = UINT32_MAX;
    for (int c = 0; c < 3; c++)
        for (int k = 0; k < 3; k++) {
            const ayfx_tmr_t *t = &a->ch[c].t[k];
            if (!fx_on(&a->ch[c], k) || !t->dur) continue;
            uint32_t left = t->cnt < t->dur ? t->dur - t->cnt : 1;
            if (left < m) m = left;
        }
    return m;
}

bool ayfx_active(const ayfx_t *a) {
    for (int c = 0; c < 3; c++)
        if (a->ch[c].row[0] & (TF_MIX | TF_FM | TF_EFM)) return true;
    return false;
}

// Все эффекты выкл (таймаут, смена режима): регистры - как записал Спектрум
void ayfx_off(ayfx_t *a) {
    static const uint8_t zero[AYFX_ROW];
    for (int c = 0; c < 3; c++) row_set(a, c, zero, 0, 0, 0);
}
