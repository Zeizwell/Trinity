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

// Текущий шаг эффекта k канала c - в AY, и длина шага
static void apply(ayfx_t *a, int c, int k) {
    ayfx_ch_t *ch = &a->ch[c];
    ayfx_tmr_t *t = &ch->t[k];
    uint8_t l = len(a, c, k);
    if (t->step >= l) t->step = 0;
    uint8_t v = a->tab[ch->row[5 + 3 * k]][t->step];
    uint32_t p = period(a, c, k);
    t->dur = p;
    if (k == 0) {
        if ((ch->row[0] & TF_MIX) == TF_SID) {              // SID: громкость = громкость ноты * уровень шага
            uint8_t base = a->regs[8 + c];
            uint8_t lvl = v & 15;
            uint8_t vol = (base & 0x10) ? (lvl ? base : 0)  //   огибающая: вкл / выкл
                                        : (uint8_t)(((base & 15) * lvl + 7) / 15);
            a->out(a->user, (uint8_t)(8 + c), vol);
            if (l == 2 && ch->row[1]) {                     // ШИМ: 2 шага делят 2P по скважности
                float lo = (ch->row[1] & 15) / 30.0f, hi = (ch->row[1] >> 4) / 30.0f, d = hi;
                if (ch->row[2]) d = lo + (hi - lo) * sweep_pos(a, ch);
                uint32_t h = clampf(2.0f * (float)p * d, 2 * p - 1);
                t->dur = t->step == 0 ? h : 2 * p - h;
            }
        } else if ((ch->row[0] & TF_MIX) == TF_SYNC) {      // Sync: перезапуск огибающей, шаг - форма (0 - текущая)
            a->out(a->user, 13, (v & 15) ? (v & 15) : (a->regs[13] & 15));
        }
    } else if (k == 1) {                                    // FM: период тона канала
        uint32_t t0 = tone(a, c);
        uint32_t np = (ch->row[0] & TF_FMPER) ? clampf((float)t0 + (float)(int8_t)v, 0x0FFF)
                                              : clampf((float)t0 * semi(v), 0x0FFF);
        a->out(a->user, (uint8_t)(2 * c), (uint8_t)np);
        a->out(a->user, (uint8_t)(2 * c + 1), (uint8_t)(np >> 8));
    } else {                                                // Env FM: период огибающей
        uint32_t e0 = env_per(a);
        uint32_t ne = (ch->row[0] & TF_EFMPER) ? clampf((float)e0 + (float)(int8_t)v, 0xFFFF)
                                               : clampf((float)e0 * semi(v), 0xFFFF);
        a->out(a->user, 11, (uint8_t)ne);
        a->out(a->user, 12, (uint8_t)(ne >> 8));
    }
}

static bool fx_on(const ayfx_ch_t *ch, int k) { return (ch->row[0] & tf_bit[k]) != 0; }

// ROW: новая строка FX канала c; выключенные эффекты отдают регистры Спектруму
static void row_set(ayfx_t *a, int c, const uint8_t *row, uint8_t shape, uint8_t start, uint8_t keep) {
    ayfx_ch_t *ch = &a->ch[c];
    uint8_t was = ch->row[0];
    memcpy(ch->row, row, AYFX_ROW);
    ch->shape = shape, ch->start = start, ch->keep = keep & 1;
    uint8_t now = ch->row[0];
    if ((was & TF_FM) && !(now & TF_FM)) restore_tone(a, c);
    if ((was & TF_EFM) && !(now & TF_EFM)) restore_env(a);
    if ((was & TF_MIX) == TF_SID && (now & TF_MIX) != TF_SID) restore_vol(a, c);
    for (int k = 0; k < 3; k++)
        if ((now & tf_bit[k]) && !(was & tf_bit[k])) {     // только что включился - с первого шага
            ch->t[k].step = 0, ch->t[k].cnt = 0;
            apply(a, c, k);
        }
}

static void packet(ayfx_t *a) {
    const uint8_t *b = a->pkt;
    switch (b[0]) {
    case AYFX_P_ROW:
        if (b[1] < 3) row_set(a, b[1], b + 2, b[16], b[17], b[18]);
        break;
    case AYFX_P_TABLE:
        memcpy(a->tab[b[1]], b + 2, AYFX_STEPS);
        break;
    case AYFX_P_RESET:                                      // новая нота: таймеры, шаги, свип - с начала
        for (int c = 0; c < 3; c++) {
            if (!(b[1] & (1 << c))) continue;
            ayfx_ch_t *ch = &a->ch[c];
            for (int k = 0; k < 3; k++) {
                ch->t[k].step = 0, ch->t[k].cnt = 0;
                if (fx_on(ch, k)) apply(a, c, k);
            }
            if (!ch->keep) ch->sweep = (uint16_t)(ch->start << 8);
        }
        break;
    case AYFX_P_INFO:
        a->info = 1;
        break;
    }
}

// ---- снаружи
void ayfx_init(ayfx_t *a, ayfx_out_fn out, void *user) {
    a->out = out, a->user = user;
    ayfx_reset(a);
}

void ayfx_reset(ayfx_t *a) {
    memset(a->ch, 0, sizeof a->ch);
    memset(a->tab, 0, sizeof a->tab);
    memset(a->regs, 0, sizeof a->regs);
    a->sel = 0, a->on = a->key = a->pos = a->need = a->info = 0;
}

// Выбран регистр reg (запись в #FFFD): ключ "TRIN" - только подряд в #C0
void ayfx_select(ayfx_t *a, uint8_t reg) {
    if (reg != 0xC0) a->key = 0;
    a->sel = reg;
}

bool ayfx_port_write(ayfx_t *a, uint8_t reg, uint8_t val) {
    static const char key[] = "TRIN";
    if (reg == 0xC0) {
        a->pos = 0;                                         // любая запись - разбор потока с начала
        if (val == 0 && a->key == 0) { a->on = 0; return true; }
        if ((char)val == key[a->key]) a->key++;
        else a->key = (char)val == key[0];
        if (a->key == 4) a->on = 1, a->key = 0;
        return true;
    }
    if (reg != 0xC1 || !a->on) return false;
    if (a->pos == 0) {                                      // код пакета -> его длина
        static const uint8_t plen[5] = { 0, 19, 18, 2, 1 };
        if (val == 0 || val > AYFX_P_INFO) return true;     // неизвестный код - пропустить байт
        a->need = plen[val];
    }
    a->pkt[a->pos++] = val;
    if (a->pos >= a->need) packet(a), a->pos = 0;
    return true;
}

// Что ответить на чтение reg (не сдвигая INFO). false - не наш (отвечает тень AY)
bool ayfx_port_peek(const ayfx_t *a, uint8_t reg, uint8_t *val) {
    if (!a->on) return false;
    if (reg == 0xC0) { *val = 'T'; return true; }
    if (reg == 0xC1) {
        static const uint8_t info[5] = { 'T', 'R', 'I', 'N', AYFX_VER };
        *val = (a->info >= 1 && a->info <= 5) ? info[a->info - 1] : 0;
        return true;
    }
    return false;
}

// Чтение reg состоялось: ответ INFO - следующий байт
void ayfx_port_read_done(ayfx_t *a, uint8_t reg) {
    if (!a->on || reg != 0xC1) return;
    if (a->info >= 1 && a->info < 5) a->info++; else a->info = 0;
}

// Чтение целиком (эмулятор): ответ и сдвиг
bool ayfx_port_read(ayfx_t *a, uint8_t reg, uint8_t *val) {
    if (!ayfx_port_peek(a, reg, val)) return false;
    ayfx_port_read_done(a, reg);
    return true;
}

bool ayfx_reg_write(ayfx_t *a, uint8_t reg, uint8_t val) {
    if (reg > 15) return false;
    a->regs[reg] = val;
    for (int c = 0; c < 3; c++) {                           // регистр сейчас у эффекта? - переложить с поправкой
        ayfx_ch_t *ch = &a->ch[c];
        if (reg == 8 + c && (ch->row[0] & TF_MIX) == TF_SID) { apply(a, c, 0); return true; }
        if ((reg == 2 * c || reg == 2 * c + 1) && fx_on(ch, 1)) { apply(a, c, 1); return true; }
        if ((reg == 11 || reg == 12) && fx_on(ch, 2)) { apply(a, c, 2); return true; }
    }
    return false;
}

void ayfx_tick(ayfx_t *a, uint32_t n) {
    for (int c = 0; c < 3; c++) {
        ayfx_ch_t *ch = &a->ch[c];
        if (!(ch->row[0] & (TF_MIX | TF_FM | TF_EFM))) continue;
        for (int k = 0; k < 3; k++) {
            if (!fx_on(ch, k)) continue;
            ayfx_tmr_t *t = &ch->t[k];
            t->cnt += n;
            while (t->dur && t->cnt >= t->dur) {            // шаги, что прошли за n тактов
                t->cnt -= t->dur;
                t->step++;
                apply(a, c, k);
            }
        }
    }
}

void ayfx_frame(ayfx_t *a) {
    for (int c = 0; c < 3; c++)
        if (a->ch[c].row[2]) a->ch[c].sweep = (uint16_t)(a->ch[c].sweep + a->ch[c].row[2] * 256);
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
