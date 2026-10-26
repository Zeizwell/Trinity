// f2lcmp - сверка FT2 с эталоном по кадрам: PSG (Bitphase Export -> PSG) против записи Unreal (*.f2l: регистры AY
// + таймеры карты, FT2 unreal/Unreal/savesnd.cpp). Начало выравнивается по первому кадру со звуком.
//   f2lcmp эталон.psg наше.f2l [кадров]   -> по каждому регистру: сколько кадров разошлось, первые расхождения
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { unsigned char r[14]; int w13; } frame_t;   // регистры 0..13, R13 записан в этом кадре

static unsigned char *load(const char *name, long *len) {
    FILE *f = fopen(name, "rb");
    if (!f) { printf("no %s\n", name); exit(1); }
    fseek(f, 0, SEEK_END);
    *len = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(*len + 1);
    fread(b, 1, *len, f);
    fclose(f);
    return b;
}

// PSG: заголовок 16 байт; FF - конец кадра, FE n - n*4 пустых кадров, иначе регистр, значение
static int load_psg(const char *name, frame_t *fr, int max) {
    long len;
    unsigned char *b = load(name, &len), reg[256] = { 0 };
    int n = 0, w13 = 0;
    for (long i = 16; i < len && n < max; ) {
        unsigned char x = b[i++];
        if (x == 0xFF) { memcpy(fr[n].r, reg, 14), fr[n].w13 = w13, n++, w13 = 0; continue; }
        if (x == 0xFE) { int k = i < len ? b[i++] * 4 : 0; while (k-- && n < max) memcpy(fr[n].r, reg, 14), fr[n].w13 = 0, n++; continue; }
        if (i >= len) break;
        reg[x] = b[i++];
        if (x == 13) w13 = 1;
    }
    free(b);
    return n;
}

// F2L: "F2LG", версия, размер записи (2), записи: регистры 14 (R13 = FF - не писали), таймеры
static int load_f2l(const char *name, frame_t *fr, int max) {
    long len;
    unsigned char *b = load(name, &len);
    if (len < 8 || memcmp(b, "F2LG", 4)) { printf("%s: not F2LG\n", name); exit(1); }
    int rec = b[6] | b[7] << 8, n = 0, chips = b[4] >= 2 ? 2 : 1, part = rec / chips, ch = 0, best = -1;
    for (int c = 0; c < chips; c++) {                       // v2 - два чипа в записи: берём тот, где звук
        int k = 0;
        for (long i = 8 + c * part; i + 14 <= len; i += rec) k += (b[i + 8] | b[i + 9] | b[i + 10]) & 0x1F ? 1 : 0;
        if (k > best) best = k, ch = c;
    }
    printf("%s: %d chip(s), taking chip %d\n", name, chips, ch);
    unsigned char r13 = 0;
    for (long i = 8 + ch * part; i + 14 <= len && n < max; i += rec, n++) {
        memcpy(fr[n].r, b + i, 14);
        fr[n].w13 = b[i + 13] != 0xFF;
        if (fr[n].w13) r13 = b[i + 13];
        fr[n].r[13] = r13;                                  // как в PSG: последнее записанное
    }
    free(b);
    return n;
}

// Первый кадр со звуком: громкость канала не 0 (или огибающая)
static int first_sound(const frame_t *fr, int n) {
    for (int i = 0; i < n; i++) if ((fr[i].r[8] | fr[i].r[9] | fr[i].r[10]) & 0x1F) return i;
    return 0;
}

static const char *const rname[14] = { "A tone lo", "A tone hi", "B tone lo", "B tone hi", "C tone lo", "C tone hi",
    "noise", "mixer", "A vol", "B vol", "C vol", "env lo", "env hi", "env shape" };

// Звучит ли регистр k в кадре f (как компаратор FT2: сравниваем только звучащее). Канал слышен - громкость не 0
static int audible1(const frame_t *f, int k) {
    int on[3], env = 0, noise = 0;
    for (int c = 0; c < 3; c++) {
        on[c] = (f->r[8 + c] & 0x1F) != 0;
        if (on[c] && (f->r[8 + c] & 0x10)) env = 1;
        if (on[c] && !(f->r[7] & (8 << c))) noise = 1;
    }
    if (k <= 5) return on[k / 2] && !(f->r[7] & (1 << (k / 2)));   // тон: канал слышен и тон включён
    if (k == 6) return noise;
    if (k == 7) return on[0] || on[1] || on[2];
    if (k <= 10) return 1;                                  // громкость - всегда
    return env;                                             // огибающая: хоть один канал на ней
}
static int audible(const frame_t *x, const frame_t *y, int k) { return audible1(x, k) || audible1(y, k); }

// Микшер: только биты слышимых каналов
static int mixer_diff(const frame_t *x, const frame_t *y) {
    int m = 0;
    for (int c = 0; c < 3; c++) if ((x->r[8 + c] | y->r[8 + c]) & 0x1F) m |= 9 << c;
    return (x->r[7] & m) != (y->r[7] & m);
}

int main(int argc, char **argv) {
    if (argc < 3) { printf("f2lcmp ref.psg ours.f2l [frames]\n"); return 1; }
    static frame_t a[200000], b[200000];
    int na = load_psg(argv[1], a, 200000);
    const char *ext = strrchr(argv[2], '.');                // наше: PSG (экспорт FT2) или F2L (запись Unreal)
    int nb = ext && (!strcmp(ext, ".psg") || !strcmp(ext, ".PSG")) ? load_psg(argv[2], b, 200000) : load_f2l(argv[2], b, 200000);
    int sa = first_sound(a, na), sb = first_sound(b, nb);
    int n = na - sa < nb - sb ? na - sa : nb - sb;
    if (argc > 3 && atoi(argv[3]) < n) n = atoi(argv[3]);
    printf("ref %d frames (sound from %d), ours %d frames (sound from %d), comparing %d\n", na, sa, nb, sb, n);
    int diff[14] = { 0 }, first[14], shown = 0;
    for (int k = 0; k < 14; k++) first[k] = -1;
    for (int i = 0; i < n; i++) {
        const frame_t *x = &a[sa + i], *y = &b[sb + i];
        int any = 0;
        for (int k = 0; k < 14; k++) {
            if (!audible(x, y, k)) continue;                // не звучит - не сравниваем
            int d = k == 13 ? (x->w13 != y->w13 || (x->w13 && x->r[13] != y->r[13])) : k == 7 ? mixer_diff(x, y)
                  : x->r[k] != y->r[k];
            if (!d) continue;
            any = 1;
            diff[k]++;
            if (first[k] < 0) first[k] = i;
        }
        if (any && shown < 20) {                            // первые расходящиеся кадры целиком
            printf("frame %6d ref ", i);
            for (int k = 0; k < 14; k++) printf("%02X ", x->r[k]);
            printf("%s\n             our ", x->w13 ? "w" : "-");
            for (int k = 0; k < 14; k++) printf(k == 13 && !y->w13 ? "-- " : "%02X ", y->r[k]);
            printf("\n");
            shown++;
        }
    }
    printf("\nregister     frames differ   first\n");
    for (int k = 0; k < 14; k++) printf("%-11s  %6d (%3d%%)   %d\n", rname[k], diff[k], n ? diff[k] * 100 / n : 0, first[k]);
    return 0;
}
