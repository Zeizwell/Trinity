// По pt2-clone: pt2_paula.c, pt2_rcfilters.c, BSD 3-Clause, (c) Olav Sorensen
#include <math.h>
#include <string.h>
#include "gs_filter.h"
#include "gs_cfg.h"

#define GS_PI 3.14159265358979

typedef struct { float a0, b1, s[2]; } pole1_t;
typedef struct { float a1, a2, b1, b2, s[2][4]; } pole2_t;

static pole1_t lp_a500, hp_a500, hp_a1200, rc16k;
static pole2_t led;
int gs_led_module;

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

static void pole1_set(pole1_t *f, double rate, double cut) {
    double b = exp(-2.0 * GS_PI * cut / rate);
    f->b1 = (float)b, f->a0 = (float)(1.0 - b), f->s[0] = f->s[1] = 0.0f;
}

static void pole2_set(pole2_t *f, double rate, double cut, double q) {
    double a = 1.0 / tan(GS_PI * cut / rate), r = 1.0 / q;
    double a1 = 1.0 / (1.0 + r * a + a * a);
    f->a1 = (float)a1, f->a2 = (float)(2.0 * a1);
    f->b1 = (float)(2.0 * (1.0 - a * a) * a1), f->b2 = (float)((1.0 - r * a + a * a) * a1);
    for (int c = 0; c < 2; c++) for (int i = 0; i < 4; i++) f->s[c][i] = 0.0f;
}

void gs_filter_init(float rate) {
    double r1 = 1e4, r2 = 1e4, c1 = 6.8e-9, c2 = 3.9e-9;                // LED: одинаков на A500 и A1200
    double sq = sqrt(r1 * r2 * c1 * c2);
    pole1_set(&lp_a500, rate, 1.0 / (2.0 * GS_PI * 360.0 * 1e-7));      // ~4421 Гц
    pole1_set(&hp_a500, rate, 1.0 / (2.0 * GS_PI * 1390.0 * 2.233e-5)); // ~5.1 Гц
    pole1_set(&hp_a1200, rate, 1.0 / (2.0 * GS_PI * 1360.0 * 2.2e-5));  // ~5.3 Гц
    pole1_set(&rc16k, rate, 1.0 / (2.0 * GS_PI * 1000.0 * 1e-8));       // ~15.9 кГц: R45/C32, R50/C47 MultiSound
    pole2_set(&led, rate, 1.0 / (2.0 * GS_PI * sq), sq / (c2 * (r1 + r2)));
    gs_led_module = 0;
    filter_rate = rate;
    memset(&bass, 0, sizeof bass);
    memset(&treble, 0, sizeof treble);
}

void gs_filter_run(float *buf, int n) {
    int use_lp = gs_cfg.lp == GS_LP_A500;           // ФНЧ A1200 ~34 кГц: не слышно, не считаем (как pt2-clone)
    int use_led = gs_cfg.led == GS_LED_ON || (gs_cfg.led == GS_LED_MODULE && gs_led_module);
    pole1_t *hp = gs_cfg.hp ? (gs_cfg.lp == GS_LP_A500 ? &hp_a500 : &hp_a1200) : 0;
    int use_rc = gs_cfg.rc16k, use_bass = gs_cfg.bass_db != 0, use_treble = gs_cfg.treble_db != 0;
    if (use_bass && bass.db != gs_cfg.bass_db) shelf_set(&bass, BASS_HZ, gs_cfg.bass_db, 0);
    if (use_treble && treble.db != gs_cfg.treble_db) shelf_set(&treble, TREBLE_HZ, gs_cfg.treble_db, 1);
    if (!use_lp && !use_led && !hp && !use_rc && !use_bass && !use_treble) return;
    int up = gs_cfg.bass_db > gs_cfg.treble_db ? gs_cfg.bass_db : gs_cfg.treble_db;
    float head = up > 0 ? (float)pow(10.0, -up / 20.0) : 1.0f;  // запас под подъём ручками: иначе пики режутся
    for (int i = 0; i < n * 2; i += 2) {
        for (int c = 0; c < 2; c++) {
            float x = buf[i + c];
            if (use_rc) x = rc16k.s[c] = x * rc16k.a0 + rc16k.s[c] * rc16k.b1;
            if (use_lp) x = lp_a500.s[c] = x * lp_a500.a0 + lp_a500.s[c] * lp_a500.b1;
            if (use_led) {
                float *s = led.s[c];
                float y = x * led.a1 + s[0] * led.a2 + s[1] * led.a1 - s[2] * led.b1 - s[3] * led.b2;
                s[1] = s[0], s[0] = x, s[3] = s[2], s[2] = y;
                x = y;
            }
            if (hp) {
                hp->s[c] = x * hp->a0 + hp->s[c] * hp->b1;
                x -= hp->s[c];
            }
            x *= head;
            if (use_bass) x = shelf_run(&bass, c, x);
            if (use_treble) x = shelf_run(&treble, c, x);
            buf[i + c] = x;
        }
    }
}
