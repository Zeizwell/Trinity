// По pt2-clone: pt2_paula.c, pt2_rcfilters.c, BSD 3-Clause, (c) Olav Sorensen
#include <math.h>
#include <string.h>
#include "gs_filter.h"
#include "gs_cfg.h"

#define GS_PI 3.14159265358979

typedef struct { float a0, b1, s[2]; double hz; } pole1_t;
typedef struct { float b0, b1, a1, a2, s[2][4]; double hz, q; } pole2_t;

static pole1_t rc, lp, hp;
static pole2_t led;
static int led_module;
void gs_filter_led_set(int on) { led_module = on; }

// Ручки НЧ и ВЧ: полки (RBJ, наклон 1)
#define BASS_HZ   200.0
#define TREBLE_HZ 4000.0
typedef struct { float b0, b1, b2, a1, a2, s[2][2]; int db; } shelf_t;
static shelf_t bass, treble;
static double filter_rate;

static void shelf_set(shelf_t *f, double cut, int db, int high) {
    double a = pow(10.0, db / 40.0), w = 2.0 * GS_PI * cut / filter_rate, c = cos(w), al = sin(w) * 0.5 * sqrt(2.0);
    double r = 2.0 * sqrt(a) * al, sg = high ? 1.0 : -1.0;
    double a0 = (a + 1.0) - sg * (a - 1.0) * c + r;
    f->b0 = (float)(a * ((a + 1.0) + sg * (a - 1.0) * c + r) / a0);
    f->b1 = (float)(-sg * 2.0 * a * ((a - 1.0) + sg * (a + 1.0) * c) / a0);
    f->b2 = (float)(a * ((a + 1.0) + sg * (a - 1.0) * c - r) / a0);
    f->a1 = (float)(sg * 2.0 * ((a - 1.0) - sg * (a + 1.0) * c) / a0);
    f->a2 = (float)(((a + 1.0) - sg * (a - 1.0) * c - r) / a0);
    f->db = db;
}

static float shelf_run(shelf_t *f, int c, float x) {
    float y = f->b0 * x + f->s[c][0];
    f->s[c][0] = f->b1 * x - f->a1 * y + f->s[c][1];
    f->s[c][1] = f->b2 * x - f->a2 * y;
    return y;
}

// Только коэффициенты: состояние не трогаем, ручка крутится на ходу без щелчка
static void pole1_set(pole1_t *f, double cut) {
    double b = exp(-2.0 * GS_PI * cut / filter_rate);
    f->b1 = (float)b, f->a0 = (float)(1.0 - b), f->hz = cut;
}

// ФНЧ 2-го порядка, совпадающий с аналоговым и у Найквиста (Vicanek, Matched Second Order Digital Filters).
// Билинейный на 48 кГц прижимает верх к 24 кГц: LED выходил темнее схемы (-3.8 дБ на 10 кГц)
static void pole2_set(pole2_t *f, double cut, double q) {
    double w = 2.0 * GS_PI * cut / filter_rate, k = 0.5 / q, e = exp(-k * w);
    double a1 = -2.0 * e * (k <= 1.0 ? cos(sqrt(1.0 - k * k) * w) : cosh(sqrt(k * k - 1.0) * w)), a2 = e * e;
    double s2 = sin(w * 0.5) * sin(w * 0.5), p0 = 1.0 - s2, p1 = s2, p2 = 4.0 * p0 * p1;
    double A0 = (1.0 + a1 + a2) * (1.0 + a1 + a2), A1 = (1.0 - a1 + a2) * (1.0 - a1 + a2), A2 = -4.0 * a2;
    double R1 = (A0 * p0 + A1 * p1 + A2 * p2) * q * q;
    double B1 = (R1 - A0 * p0) / p1;
    double b0 = 0.5 * (sqrt(A0) + sqrt(B1 > 0.0 ? B1 : 0.0));
    f->b0 = (float)b0, f->b1 = (float)(sqrt(A0) - b0), f->a1 = (float)a1, f->a2 = (float)a2;
    f->hz = cut, f->q = q;
}

// Настройка поменялась - новые коэффициенты
static void stages_update(void) {
    const gs_stage_t *st = gs_cfg.st;
    if (rc.hz != st[GS_ST_RC].hz) pole1_set(&rc, st[GS_ST_RC].hz);
    if (lp.hz != st[GS_ST_LP].hz) pole1_set(&lp, st[GS_ST_LP].hz);
    if (hp.hz != st[GS_ST_HP].hz) pole1_set(&hp, st[GS_ST_HP].hz);
    if (led.hz != st[GS_ST_LED].hz || led.q != st[GS_ST_LED].q) pole2_set(&led, st[GS_ST_LED].hz, st[GS_ST_LED].q);
}

void gs_filter_init(float rate) {
    memset(&rc, 0, sizeof rc), memset(&lp, 0, sizeof lp), memset(&hp, 0, sizeof hp), memset(&led, 0, sizeof led);
    led_module = 0;
    filter_rate = rate;
    memset(&bass, 0, sizeof bass);
    memset(&treble, 0, sizeof treble);
}

// Ручка плюс окраска режима: в пределах -12..12 дБ
static int tone_db(int db) { return db > GS_TONE_DB_MAX ? GS_TONE_DB_MAX : db < -GS_TONE_DB_MAX ? -GS_TONE_DB_MAX : db; }

// --- кривая для драйвера: те же коэффициенты, что звучат ---
static double abs2(double re, double im) { return re * re + im * im; }

static double pole1_lp2(const pole1_t *f, double w) {
    return f->a0 * f->a0 / abs2(1.0 - f->b1 * cos(w), f->b1 * sin(w));
}

static double shelf_db(const shelf_t *f, double w) {
    double c = cos(w), s = sin(w), c2 = cos(2.0 * w), s2 = sin(2.0 * w);
    return 10.0 * log10(abs2(f->b0 + f->b1 * c + f->b2 * c2, f->b1 * s + f->b2 * s2) / abs2(1.0 + f->a1 * c + f->a2 * c2, f->a1 * s + f->a2 * s2));
}

static double hp_p(double c, double s) {             // ФВЧ: вход минус ФНЧ. H = 1 - a0 / (1 - b1 z^-1)
    double dr = 1.0 - hp.b1 * c, di = hp.b1 * s, d = abs2(dr, di);
    return abs2(1.0 - hp.a0 * dr / d, hp.a0 * di / d);
}

// Полка для кривой: свои коэффициенты. Звуковые обновятся только на следующем блоке, а кривую просят сразу после ручки
static float tone_curve_db(int db, double cut, int high, double w) {
    shelf_t f;
    db = tone_db(db);
    if (!db) return 0.0f;
    shelf_set(&f, cut, db, high);
    return (float)shelf_db(&f, w);
}

float gs_filter_stage_db(int stage, float hz) {
    const gs_stage_t *st = gs_cfg.st;
    double w = 2.0 * GS_PI * hz / filter_rate, c = cos(w), s = sin(w), p = 1.0;
    stages_update();
    switch (stage) {
    case GS_ST_RC: if (st[GS_ST_RC].on) p = pole1_lp2(&rc, w); break;
    case GS_ST_LP: if (st[GS_ST_LP].on) p = pole1_lp2(&lp, w); break;
    case GS_ST_LED:
        if (gs_cfg.led == GS_LED_ON || (gs_cfg.led == GS_LED_MODULE && led_module))
            p = abs2(led.b0 + led.b1 * c, led.b1 * s) / abs2(1.0 + led.a1 * c + led.a2 * cos(2.0 * w), led.a1 * s + led.a2 * sin(2.0 * w));
        break;
    case GS_ST_HP: if (st[GS_ST_HP].on) p = hp_p(c, s); break;
    case GS_CURVE_BASS: return tone_curve_db(gs_cfg.bass_db + gs_cfg.color_bass_db, BASS_HZ, 0, w);
    case GS_CURVE_TREBLE: return tone_curve_db(gs_cfg.treble_db + gs_cfg.color_treble_db, TREBLE_HZ, 1, w);
    default: break;
    }
    return (float)(10.0 * log10(p));
}

float gs_filter_db(float hz) {
    float db = 0.0f;
    for (int i = 0; i < GS_CURVE_COUNT; i++) db += gs_filter_stage_db(i, hz);
    return db;
}

// Компрессор (общий на L и R, чтобы не гуляла панорама) и мягкий ограничитель. Порог, степень и подъём - здесь
#define COMP_THR     0.10f      // порог, -20 дБ
#define COMP_RATIO   3.0f
#define COMP_MAKEUP  3.2f       // подъём, ~+10 дБ
#define COMP_ATK_MS  5.0f
#define COMP_REL_MS  150.0f
#define LIM_KNEE     0.8f       // до него ограничитель не трогает

#define COMP_STEP    16         // усиление считаем раз в столько отсчётов (степень на RP дорогая), между ними - плавно

static float comp_env, comp_g = COMP_MAKEUP, comp_add;
static int comp_left;

static float soft_limit(float x) {
    float a = fabsf(x);
    if (a <= LIM_KNEE) return x;
    a = LIM_KNEE + (1.0f - LIM_KNEE) * tanhf((a - LIM_KNEE) / (1.0f - LIM_KNEE));
    return x < 0.0f ? -a : a;
}

void gs_comp_run(float *buf, int n, float rate) {
    if (!gs_cfg.comp) return;
    static float atk, rel, atk_rate;
    if (rate != atk_rate) atk_rate = rate, atk = 1.0f - expf(-1000.0f / (COMP_ATK_MS * rate)), rel = 1.0f - expf(-1000.0f / (COMP_REL_MS * rate));
    for (int i = 0; i < n * 2; i += 2) {
        float a = fabsf(buf[i]), b = fabsf(buf[i + 1]), x = a > b ? a : b;
        comp_env += (x - comp_env) * (x > comp_env ? atk : rel);
        if (comp_left-- <= 0) {
            float g = comp_env > COMP_THR ? powf(COMP_THR / comp_env, 1.0f - 1.0f / COMP_RATIO) : 1.0f;
            comp_add = (g * COMP_MAKEUP - comp_g) / COMP_STEP;
            comp_left = COMP_STEP - 1;
        }
        comp_g += comp_add;
        buf[i] = soft_limit(buf[i] * comp_g);
        buf[i + 1] = soft_limit(buf[i + 1] * comp_g);
    }
}

void gs_filter_run(float *buf, int n) {
    int use_lp = gs_cfg.st[GS_ST_LP].on, use_hp = gs_cfg.st[GS_ST_HP].on, use_rc = gs_cfg.st[GS_ST_RC].on;
    int use_led = gs_cfg.led == GS_LED_ON || (gs_cfg.led == GS_LED_MODULE && led_module);
    stages_update();
    int bass_db = tone_db(gs_cfg.bass_db + gs_cfg.color_bass_db), treble_db = tone_db(gs_cfg.treble_db + gs_cfg.color_treble_db);
    int use_bass = bass_db != 0, use_treble = treble_db != 0;
    if (use_bass && bass.db != bass_db) shelf_set(&bass, BASS_HZ, bass_db, 0);
    if (use_treble && treble.db != treble_db) shelf_set(&treble, TREBLE_HZ, treble_db, 1);
    if (!use_lp && !use_led && !use_hp && !use_rc && !use_bass && !use_treble) return;
    int up = bass_db > treble_db ? bass_db : treble_db;
    static int head_up;
    static float head = 1.0f;  // запас под подъём ручками: иначе пики режутся
    if (up != head_up) head_up = up, head = up > 0 ? (float)pow(10.0, -up / 20.0) : 1.0f;
    for (int i = 0; i < n * 2; i += 2) {
        for (int c = 0; c < 2; c++) {
            float x = buf[i + c];
            if (use_rc) x = rc.s[c] = x * rc.a0 + rc.s[c] * rc.b1;
            if (use_lp) x = lp.s[c] = x * lp.a0 + lp.s[c] * lp.b1;
            if (use_led) {
                float *s = led.s[c];
                float y = x * led.b0 + s[0] * led.b1 - s[1] * led.a1 - s[2] * led.a2;
                s[0] = x, s[2] = s[1], s[1] = y;
                x = y;
            }
            if (use_hp) {
                hp.s[c] = x * hp.a0 + hp.s[c] * hp.b1;
                x -= hp.s[c];
            }
            x *= head;
            if (use_bass) x = shelf_run(&bass, c, x);
            if (use_treble) x = shelf_run(&treble, c, x);
            buf[i + c] = x;
        }
    }
}
