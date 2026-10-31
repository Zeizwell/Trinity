// Понижение частоты в 2 раза: полуполосный FIR на 59 точек. Коэффициенты - pt2-clone: pt2_downsample2x.c, BSD 3-Clause, (c) Olav Sorensen
#include "gs_down.h"

#define TAPS 59
#define CENTER 29

static const float coef[15] = {     // при расстоянии 1, 3, 5... от центра; в центре 0.5
    0.31679609962928f, -0.10163877066856f, 0.05646939759172f, -0.03589869172828f, 0.02384893442862f,
    -0.01596102646846f, 0.01054794795196f, -0.00678935474634f, 0.00420731862183f, -0.00248066436637f,
    0.00137207386220f, -0.00069823637245f, 0.00031710491117f, -0.00012143320790f, 0.00003501888526f
};

// История - кольцо с зеркалом: окно из TAPS отсчётов всегда подряд, сдвигать не надо
static float hist[2][TAPS * 2];
static int pos;

void gs_down_reset(void) {
    for (int c = 0; c < 2; c++) for (int i = 0; i < TAPS * 2; i++) hist[c][i] = 0.0f;
    pos = 0;
}

static void put(float *h, int i, float x) { h[i] = x, h[i + TAPS] = x; }

// in - 2n стерео-отсчётов, out - n. Можно на месте
void gs_down_run(const float *in, float *out, int n) {
    for (int i = 0; i < n; i++) {
        pos -= 2;
        if (pos < 0) pos += TAPS;
        int older = pos + 1 < TAPS ? pos + 1 : 0;
        for (int c = 0; c < 2; c++) {
            float a = in[i * 4 + c], b = in[i * 4 + 2 + c];     // читаем до записи: можно на месте
            put(hist[c], older, a);
            put(hist[c], pos, b);
            const float *h = &hist[c][pos];
            float y = 0.5f * h[CENTER];
            for (int k = 0; k < 15; k++) y += coef[k] * (h[CENTER - 1 - 2 * k] + h[CENTER + 1 + 2 * k]);
            out[i * 2 + c] = y;
        }
    }
}
