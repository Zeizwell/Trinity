// Логика - реплеер pt2-clone, pt2_replayer.c. Copyright (c) 2010-2026, Olav Sorensen, BSD 3-Clause. Причуды PT сохранены.
// Сверено с оригиналом по тикам (doc/paula.md). Не сделано: E8x (по умолчанию выключен и в pt2-clone).
#include <stddef.h>
#include "gs_pt.h"

#define VOICES 4
#define ROWS 64
#define SAMPLES 31
#define CIA_HZ (3546895.0 / 5.0)

typedef struct {
    const int8_t *start;
    uint16_t len, rep, replen;  // слова
    uint8_t fine, vol;
} smp_t;

typedef struct {
    const int8_t *n_start, *n_loopstart;
    int8_t *n_wavestart;
    int8_t n_volume, n_toneportdirec, n_pattpos, n_loopcount;
    uint8_t n_wavecontrol, n_glissfunk, n_sampleoffset, n_toneportspeed;
    uint8_t n_vibratocmd, n_tremolocmd, n_finetune, n_funkoffset;
    uint8_t n_vibratopos, n_tremolopos;
    int16_t n_period, n_wantedperiod;
    uint16_t n_note, n_cmd, n_length, n_replen;
    uint8_t index;
} chan_t;

gs_pt_reg_t gs_pt_reg[VOICES];

static const uint8_t *patterns, *order;
static int song_len, can_write;
static smp_t smp[SAMPLES];
static chan_t chans[VOICES];
static int tick, speed, bpm, cia_bpm, stopped, led = -1;
static int8_t row, break_pos;
static uint8_t patt, patt_del, patt_del2, low_mask, break_flag, jump;
static int16_t pos;
static int play_pos, play_row;     // строка, которая сейчас играет
static uint8_t dma_mask;

static const uint8_t funk_tab[16] = {
    0x00, 0x05, 0x06, 0x07, 0x08, 0x0A, 0x0B, 0x0D, 0x10, 0x13, 0x16, 0x1A, 0x20, 0x2B, 0x40, 0x80
};

static const uint8_t vib_tab[32] = {
    0x00, 0x18, 0x31, 0x4A, 0x61, 0x78, 0x8D, 0xA1, 0xB4, 0xC5, 0xD4, 0xE0, 0xEB, 0xF4, 0xFA, 0xFD,
    0xFF, 0xFD, 0xFA, 0xF4, 0xEB, 0xE0, 0xD4, 0xC5, 0xB4, 0xA1, 0x8D, 0x78, 0x61, 0x4A, 0x31, 0x18
};

// 16 настроек по 37 нот (последняя - 0) и 15 слов запаса: арпеджио при настройке -1 вылезает за таблицу, как в PT
static const int16_t period_tab[37 * 16 + 15] = {
#include "gs_pt_periods.inc"
};

static void update_funk(chan_t *c) {
    int sp = c->n_glissfunk >> 4;
    if (!sp) return;
    c->n_funkoffset += funk_tab[sp];
    if (c->n_funkoffset >= 128) {
        c->n_funkoffset = 0;
        if (c->n_loopstart && c->n_wavestart) {
            if (++c->n_wavestart >= c->n_loopstart + (c->n_replen << 1)) c->n_wavestart = (int8_t *)c->n_loopstart;
            if (can_write) *c->n_wavestart = (int8_t)(-1 - *c->n_wavestart);
        }
    }
}

static void jump_loop(chan_t *c) {
    if (tick) return;
    if (!(c->n_cmd & 0xF)) c->n_pattpos = row;
    else {
        if (!c->n_loopcount) c->n_loopcount = c->n_cmd & 0xF;
        else if (!--c->n_loopcount) return;
        break_pos = c->n_pattpos;
        break_flag = 1;
    }
}

static void retrigger(chan_t *c) {
    gs_pt_reg_t *r = &gs_pt_reg[c->index];
    r->lc = c->n_start, r->len = c->n_length, r->period = (uint16_t)c->n_period;
    gs_pt_dma_start(c->index);
    r->lc = c->n_loopstart, r->len = c->n_replen;
}

static void vol_slide(chan_t *c) {
    int p = c->n_cmd & 0xFF;
    if (!(p & 0xF0)) {
        c->n_volume -= p & 0xF;
        if (c->n_volume < 0) c->n_volume = 0;
    } else {
        c->n_volume += p >> 4;
        if (c->n_volume > 64) c->n_volume = 64;
    }
}

static void arpeggio(chan_t *c) {
    int t = tick % 3, add;
    if (t == 0) { gs_pt_reg[c->index].period = (uint16_t)c->n_period; return; }
    add = t == 1 ? c->n_cmd >> 4 : c->n_cmd & 0xF;
    add &= 0xF;
    const int16_t *p = &period_tab[c->n_finetune * 37];
    for (int n = 0; n < 37; n++)
        if (c->n_period >= p[n]) { gs_pt_reg[c->index].period = (uint16_t)p[n + add]; break; }
}

static void porta_up(chan_t *c) {
    c->n_period -= (c->n_cmd & 0xFF) & low_mask;
    low_mask = 0xFF;
    if ((c->n_period & 0xFFF) < 113) c->n_period = (int16_t)((c->n_period & 0xF000) | 113);   // PT: знак снят до сравнения
    gs_pt_reg[c->index].period = c->n_period & 0xFFF;
}

static void porta_down(chan_t *c) {
    c->n_period += (c->n_cmd & 0xFF) & low_mask;
    low_mask = 0xFF;
    if ((c->n_period & 0xFFF) > 856) c->n_period = (int16_t)((c->n_period & 0xF000) | 856);
    gs_pt_reg[c->index].period = c->n_period & 0xFFF;
}

static int period_index(const int16_t *p, int v) {
    for (int i = 0; i < 37; i++)
        if (v >= p[i]) return i;
    return 35;
}

static void set_tone_porta(chan_t *c) {
    const int16_t *p = &period_tab[c->n_finetune * 37];
    int i = period_index(p, c->n_note & 0xFFF);
    if ((c->n_finetune & 8) && i > 0) i--;
    c->n_wantedperiod = p[i];
    c->n_toneportdirec = 0;
    if (c->n_period == c->n_wantedperiod) c->n_wantedperiod = 0;
    else if (c->n_period > c->n_wantedperiod) c->n_toneportdirec = 1;
}

static void tone_no_change(chan_t *c) {
    if (c->n_wantedperiod <= 0) return;
    if (c->n_toneportdirec > 0) {
        c->n_period -= c->n_toneportspeed;
        if (c->n_period <= c->n_wantedperiod) c->n_period = c->n_wantedperiod, c->n_wantedperiod = 0;
    } else {
        c->n_period += c->n_toneportspeed;
        if (c->n_period >= c->n_wantedperiod) c->n_period = c->n_wantedperiod, c->n_wantedperiod = 0;
    }
    if (!(c->n_glissfunk & 0xF)) gs_pt_reg[c->index].period = (uint16_t)c->n_period;
    else {
        const int16_t *p = &period_tab[c->n_finetune * 37];
        gs_pt_reg[c->index].period = (uint16_t)p[period_index(p, c->n_period)];
    }
}

static void tone_porta(chan_t *c) {
    if (c->n_cmd & 0xFF) {
        c->n_toneportspeed = c->n_cmd & 0xFF;
        c->n_cmd &= 0xFF00;
    }
    tone_no_change(c);
}

static void vibrato2(chan_t *c) {
    int vpos = (c->n_vibratopos >> 2) & 0x1F, type = c->n_wavecontrol & 3;
    uint16_t d;
    if (type == 0) d = vib_tab[vpos];
    else if (type == 1) d = c->n_vibratopos < 128 ? (uint16_t)(vpos << 3) : (uint16_t)(255 - (vpos << 3));
    else d = 255;
    d = (uint16_t)((d * (c->n_vibratocmd & 0xF)) >> 7);
    gs_pt_reg[c->index].period = c->n_vibratopos < 128 ? (uint16_t)(c->n_period + d) : (uint16_t)(c->n_period - d);
    c->n_vibratopos += (c->n_vibratocmd >> 2) & 0x3C;
}

static void vibrato(chan_t *c) {
    if (c->n_cmd & 0x0F) c->n_vibratocmd = (c->n_vibratocmd & 0xF0) | (c->n_cmd & 0x0F);
    if (c->n_cmd & 0xF0) c->n_vibratocmd = (c->n_cmd & 0xF0) | (c->n_vibratocmd & 0x0F);
    vibrato2(c);
}

static void tremolo(chan_t *c) {
    if (c->n_cmd & 0x0F) c->n_tremolocmd = (c->n_tremolocmd & 0xF0) | (c->n_cmd & 0x0F);
    if (c->n_cmd & 0xF0) c->n_tremolocmd = (c->n_cmd & 0xF0) | (c->n_tremolocmd & 0x0F);
    int tpos = (c->n_tremolopos >> 2) & 0x1F, type = (c->n_wavecontrol >> 4) & 3, d;
    if (type == 0) d = vib_tab[tpos];
    else if (type == 1) d = c->n_vibratopos < 128 ? tpos << 3 : 255 - (tpos << 3);  // PT: и правда n_vibratopos
    else d = 255;
    d = (d * (c->n_tremolocmd & 0xF)) >> 6;
    if (c->n_tremolopos < 128) { d = c->n_volume + d; if (d > 64) d = 64; }
    else { d = c->n_volume - d; if (d < 0) d = 0; }
    gs_pt_reg[c->index].vol = (uint16_t)d;
    c->n_tremolopos += (c->n_tremolocmd >> 2) & 0x3C;
}

static void sample_offset(chan_t *c) {
    if (c->n_cmd & 0xFF) c->n_sampleoffset = c->n_cmd & 0xFF;
    uint16_t off = (uint16_t)(c->n_sampleoffset << 7);
    if (off < c->n_length) {
        c->n_length -= off;
        c->n_start += off << 1;
    } else c->n_length = 1;
}

static void e_commands(chan_t *c) {
    int p = c->n_cmd & 0xF;
    switch ((c->n_cmd & 0xF0) >> 4) {
    case 0x0: if (!tick) led = (c->n_cmd & 1) ^ 1; break;
    case 0x1: if (!tick) { low_mask = 0xF; porta_up(c); } break;
    case 0x2: if (!tick) { low_mask = 0xF; porta_down(c); } break;
    case 0x3: c->n_glissfunk = (uint8_t)((c->n_glissfunk & 0xF0) | p); break;
    case 0x4: c->n_wavecontrol = (uint8_t)((c->n_wavecontrol & 0xF0) | p); break;
    case 0x5: c->n_finetune = (uint8_t)p; break;
    case 0x6: jump_loop(c); break;
    case 0x7: c->n_wavecontrol = (uint8_t)((p << 4) | (c->n_wavecontrol & 0xF)); break;
    case 0x9:
        if (p) {
            if (tick == 0 && (c->n_note & 0xFFF)) break;
            if (tick % p == 0) retrigger(c);
        }
        break;
    case 0xA: if (!tick) { c->n_volume += p; if (c->n_volume > 64) c->n_volume = 64; } break;
    case 0xB: if (!tick) { c->n_volume -= p; if (c->n_volume < 0) c->n_volume = 0; } break;
    case 0xC: if (tick == p) c->n_volume = 0; break;
    case 0xD: if (tick == p && (c->n_note & 0xFFF)) retrigger(c); break;
    case 0xE: if (tick == 0 && patt_del2 == 0) patt_del = (uint8_t)(p + 1); break;
    case 0xF:
        if (!tick) {
            c->n_glissfunk = (uint8_t)((p << 4) | (c->n_glissfunk & 0xF));
            if (c->n_glissfunk & 0xF0) update_funk(c);
        }
        break;
    default: break;
    }
}

static void more_effects(chan_t *c) {
    int cmd = (c->n_cmd & 0xF00) >> 8, p = c->n_cmd & 0xFF;
    switch (cmd) {
    case 0x9: sample_offset(c); return;
    case 0xB:
        pos = (int16_t)(p - 1);                         // B00: -1, дальше станет 0
        break_pos = 0;
        jump = 1;
        return;
    case 0xD:
        break_pos = (int8_t)(((p & 0xF0) >> 4) * 10 + (p & 0xF));
        if ((uint8_t)break_pos > 63) break_pos = 0;
        jump = 1;
        return;
    case 0xE: e_commands(c); return;
    case 0xF:
        if (!p) stopped = 1;
        else if (p < 32) speed = p, tick = 0;
        else cia_bpm = p;                               // CIA берёт новый таймер со следующего прерывания
        return;
    case 0xC:
        c->n_volume = (int8_t)p;
        if ((uint8_t)c->n_volume > 64) c->n_volume = 64;
        return;
    default: gs_pt_reg[c->index].period = (uint16_t)c->n_period;
    }
}

static void check_effects(chan_t *c) {
    int cmd = (c->n_cmd & 0xF00) >> 8;
    update_funk(c);
    if (c->n_cmd & 0xFFF) {
        switch (cmd) {
        case 0x0: arpeggio(c); break;
        case 0x1: porta_up(c); break;
        case 0x2: porta_down(c); break;
        case 0x3: tone_porta(c); break;
        case 0x4: vibrato(c); break;
        case 0x5: tone_no_change(c); vol_slide(c); break;
        case 0x6: vibrato2(c); vol_slide(c); break;
        case 0xE: e_commands(c); break;
        default:
            gs_pt_reg[c->index].period = (uint16_t)c->n_period;
            if (cmd == 0x7) tremolo(c);
            else if (cmd == 0xA) vol_slide(c);
        }
    }
    if (cmd != 0x7) gs_pt_reg[c->index].vol = (uint16_t)c->n_volume;   // тремоло громкость уже поставил
}

static void set_period(chan_t *c) {
    int i, note = c->n_note & 0xFFF;
    for (i = 0; i < 37; i++)
        if (note >= period_tab[i]) break;
    c->n_period = period_tab[c->n_finetune * 37 + i];
    if ((c->n_cmd & 0xFF0) != 0xED0) {
        gs_pt_reg_t *r = &gs_pt_reg[c->index];
        if (!(c->n_wavecontrol & 0x04)) c->n_vibratopos = 0;
        if (!(c->n_wavecontrol & 0x40)) c->n_tremolopos = 0;
        r->len = c->n_length, r->lc = c->n_start;
        if (!c->n_start) {
            c->n_loopstart = NULL;
            r->len = 1;
            c->n_replen = 1;
        }
        r->period = (uint16_t)c->n_period;
        dma_mask |= (uint8_t)(1 << c->index);
    }
    more_effects(c);
}

static void play_voice(chan_t *c) {
    if (!c->n_note && !c->n_cmd) gs_pt_reg[c->index].period = (uint16_t)c->n_period;

    const uint8_t *b = patterns + (patt * ROWS * VOICES + row * VOICES + c->index) * 4;
    int num = (b[0] & 0xF0) | (b[2] >> 4);
    c->n_note = (uint16_t)((b[0] & 0xF) << 8 | b[1]);
    c->n_cmd = (uint16_t)((b[2] & 0xF) << 8 | b[3]);

    if (num >= 1 && num <= SAMPLES) {
        const smp_t *s = &smp[num - 1];
        c->n_start = s->start;
        c->n_finetune = s->fine;
        c->n_volume = (int8_t)s->vol;
        c->n_length = s->len;
        c->n_replen = s->replen;
        if (s->rep) {
            c->n_loopstart = c->n_start + (s->rep << 1);
            c->n_length = (uint16_t)(s->rep + c->n_replen);
        } else c->n_loopstart = c->n_start;
        c->n_wavestart = (int8_t *)c->n_loopstart;
        if (!c->n_length) c->n_loopstart = NULL, c->n_wavestart = NULL;
    }

    if (c->n_note & 0xFFF) {
        int cmd = (c->n_cmd & 0xF00) >> 8;
        if ((c->n_cmd & 0xFF0) == 0xE50) {
            c->n_finetune = c->n_cmd & 0xF;
            set_period(c);
        } else if (cmd == 3 || cmd == 5) {
            set_tone_porta(c);
            more_effects(c);
        } else if (cmd == 9) {
            more_effects(c);
            set_period(c);
        } else set_period(c);
    } else more_effects(c);
}

static void next_position(void) {
    row = break_pos;
    break_pos = 0;
    jump = 0;
    pos = (int16_t)((pos + 1) & 127);
    if (pos >= song_len) pos = 0;
    patt = order[pos];
}

void gs_pt_tick(void) {
    if (cia_bpm != -1) {
        bpm = cia_bpm;
        cia_bpm = -1;
    }
    if (++tick >= speed) {
        tick = 0;
        if (patt_del2 == 0) {
            play_pos = pos, play_row = row;
            dma_mask = 0;
            for (int i = 0; i < VOICES; i++) {
                play_voice(&chans[i]);
                gs_pt_reg[i].vol = (uint16_t)chans[i].n_volume;
            }
            for (int i = 0; i < VOICES; i++) {
                if (dma_mask >> i & 1) gs_pt_dma_start(i);
                gs_pt_reg[i].lc = chans[i].n_loopstart, gs_pt_reg[i].len = chans[i].n_replen;
            }
        } else {
            for (int i = 0; i < VOICES; i++) check_effects(&chans[i]);
        }
        row++;
        if (patt_del) patt_del2 = patt_del, patt_del = 0;
        if (patt_del2) {
            if (--patt_del2) row--;
        }
        if (break_flag) {
            row = break_pos;
            break_pos = 0;
            break_flag = 0;
        }
        if (row >= ROWS || jump) next_position();
    } else {
        for (int i = 0; i < VOICES; i++) check_effects(&chans[i]);
        if (jump) next_position();
    }
}

int gs_pt_is_mod(const void *data, int size) {
    const uint8_t *d = data;
    if (size < 1084) return 0;
    d += 1080;
    return (d[0] == 'M' && d[1] == '.' && d[2] == 'K' && d[3] == '.') || (d[0] == 'M' && d[1] == '!' && d[2] == 'K' && d[3] == '!') ||
           (d[0] == 'F' && d[1] == 'L' && d[2] == 'T' && d[3] == '4') || (d[0] == '4' && d[1] == 'C' && d[2] == 'H' && d[3] == 'N');
}

int gs_pt_load(const void *data, int size, int writable) {
    const uint8_t *d = data;
    int top = 0;
    if (!gs_pt_is_mod(data, size)) return 0;
    song_len = d[950];
    if (song_len < 1 || song_len > 128) return 0;
    order = d + 952;
    for (int i = 0; i < 128; i++)
        if (order[i] > top) top = order[i];
    long off = 1084 + (long)(top + 1) * 1024;
    if (off > size) return 0;
    patterns = d + 1084;
    can_write = writable;
    for (int i = 0; i < SAMPLES; i++) {                 // концы и циклы - внутри файла: дальше читать нечего
        const uint8_t *h = d + 20 + i * 30;
        smp_t *s = &smp[i];
        long len = (long)(h[22] << 8 | h[23]) * 2, ls = (long)(h[26] << 8 | h[27]) * 2, ll = (long)(h[28] << 8 | h[29]) * 2;
        if (off + len > size) len = (size - off) & ~1L;
        if (len < 0) len = 0;
        if (ll < 2) ll = 2;
        if (ls >= len) ls = 0, ll = 2;
        else if (ls + ll > len) ll = (len - ls) < 2 ? 2 : len - ls;
        if (len < 2) ls = 0, ll = 2;
        s->start = (const int8_t *)d + off;
        s->len = (uint16_t)(len >> 1), s->rep = (uint16_t)(ls >> 1), s->replen = (uint16_t)(ll >> 1);
        s->fine = h[24] & 0xF;
        s->vol = h[25] > 64 ? 64 : h[25];
        off += len;
    }
    for (int i = 0; i < VOICES; i++) {
        chan_t *c = &chans[i];
        const uint8_t *z = (const uint8_t *)c;
        for (size_t k = 0; k < sizeof *c; k++) ((uint8_t *)z)[k] = 0;
        c->index = (uint8_t)i;
        gs_pt_reg[i].period = 0, gs_pt_reg[i].vol = 0, gs_pt_reg[i].lc = NULL, gs_pt_reg[i].len = 0;
    }
    speed = 6, bpm = 125, cia_bpm = -1, stopped = 0, led = -1;
    row = 0, break_pos = 0, patt_del = patt_del2 = 0, break_flag = jump = 0, low_mask = 0xFF;
    pos = 0, patt = order[0], play_pos = play_row = 0;
    tick = speed - 1;                                   // первый тик - сразу нулевой
    return 1;
}

void gs_pt_set_pos(int p) {
    if (p >= 0 && p < song_len) pos = (int16_t)p, patt = order[p];
}
void gs_pt_set_speed(int v) { if (v > 0) speed = v; }
void gs_pt_set_bpm(int v) { if (v >= 32 && v <= 255) bpm = v; }
int gs_pt_pos(void) { return pos; }
int gs_pt_row(void) { return row; }
int gs_pt_play_pos(void) { return play_pos; }
int gs_pt_play_row(void) { return play_row; }
int gs_pt_speed(void) { return speed; }
int gs_pt_bpm(void) { return bpm; }
int gs_pt_delay(void) { return patt_del2; }
int gs_pt_stopped(void) { return stopped; }
int gs_pt_led(void) { int v = led; led = -1; return v; }
double gs_pt_tick_sec(void) { return (1773447 / bpm + 1) / CIA_HZ; }      // да, PT отбрасывает дробь
int gs_pt_period(int chan) { return chans[chan].n_period & 0xFFF; }
