// Наш плеер gs_pt против настоящего реплеера ProTracker: все регистры Паулы на каждом тике
#include <stdio.h>
#include <windows.h>
#include "stub.h"
#include "../../engine/gs/gs_pt.h"

editor_t editor; cursor_t cursor; ui_t ui; config_t config; audio_t audio;
static module_t mod; module_t *song = &mod;
extern int ptcmp_bpm(void), ptcmp_pos(void), ptcmp_delay(void);

static int reg_per[4], reg_vol[4], reg_trig[4], reg_len[4];
static const int8_t *reg_lc[4];
static const int8_t *trig_lc[2][4];
static int trig_len[2][4], trig_per[2][4];
static int8_t null_smp[4];
int8_t *paulaGetNullSamplePtr(void) { return null_smp; }
void paulaWriteByte(uint32_t a, uint8_t d) { (void)a, (void)d; }
void paulaWritePtr(uint32_t a, const int8_t *p) { reg_lc[(a >> 4 & 15) - 10] = p; }
void paulaWriteWord(uint32_t a, uint16_t d) {
    if (a == 0xDFF096) {
        if (d & 0x8000) for (int i = 0; i < 4; i++) if (d >> i & 1) {
            reg_trig[i]++;
            trig_lc[0][i] = reg_lc[i], trig_len[0][i] = reg_len[i], trig_per[0][i] = reg_per[i];
        }
        return;
    }
    int c = (a >> 4 & 15) - 10, r = a & 15;
    if (r == 4) reg_len[c] = d;
    else if (r == 6) reg_per[c] = d;
    else if (r == 8) { int v = d & 127; reg_vol[c] = v > 64 ? 64 : v; }
}
static int my_trig[4];
void gs_pt_dma_start(int i) {
    my_trig[i]++;
    trig_lc[1][i] = gs_pt_reg[i].lc, trig_len[1][i] = gs_pt_reg[i].len, trig_per[1][i] = gs_pt_reg[i].period;
}

void gs_pt_led_set(int on) { (void)on; }

static unsigned char *d1, *d2;
static int size;

// Стресс: случайные ноты и ВСЕ команды, в том числе причуды
static unsigned rng = 12345;
static int rnd(int n) { rng = rng * 1664525u + 1013904223u; return (rng >> 8) % n; }
static const short notes[36] = {856,808,762,720,678,640,604,570,538,508,480,453,428,404,381,360,339,320,302,285,269,254,240,226,214,202,190,180,170,160,151,143,135,127,120,113};
static void make_stress(unsigned seed) {
    rng = seed;
    int pats = 8, smpb = 0, slen[31];
    size = 1084 + pats * 1024;
    for (int i = 0; i < 31; i++) { slen[i] = i < 12 ? 200 + rnd(3000) : 0; smpb += slen[i] * 2; }
    d1 = calloc(size + smpb + 64, 1);
    for (int i = 0; i < 31; i++) {
        unsigned char *h = d1 + 20 + i * 30;
        h[22] = slen[i] >> 8, h[23] = slen[i];
        h[24] = rnd(16), h[25] = rnd(70);
        if (slen[i] && rnd(2)) { int ls = rnd(slen[i]), ll = 1 + rnd(slen[i] - ls + 20); h[26] = ls >> 8, h[27] = ls, h[28] = ll >> 8, h[29] = ll; }
        else h[29] = 1;
    }
    d1[950] = 12;
    for (int i = 0; i < 128; i++) d1[952 + i] = i < 12 ? rnd(pats) : 0;
    d1[952 + 7] = 7;                                                                     // старший паттерн есть всегда: сэмплы там, где ждём
    memcpy(d1 + 1080, "M.K.", 4);
    for (int i = 0; i < pats * 256; i++) {
        unsigned char *b = d1 + 1084 + i * 4;
        int per = rnd(3) ? notes[rnd(36)] : 0, s = rnd(3) == 0 ? rnd(32) : 0, cmd = rnd(3) ? rnd(16) : 0, par = rnd(256);
        if (cmd == 14) { int x = rnd(16); if (x == 8) x = 9; par = x << 4 | rnd(16); }  // E8x выключен в обоих
        if (cmd == 15 && !par) par = 6;                                                  // F00 останавливает песню
        if (cmd == 15 && par < 32 && par > 12) par = 12;
        if (rnd(40) == 0) per = 100 + rnd(800);                                          // кривой период
        b[0] = (s & 0xF0) | per >> 8, b[1] = per, b[2] = (s & 15) << 4 | cmd, b[3] = par;
    }
    for (int i = 0; i < smpb; i++) d1[size + i] = rnd(256);
    size += smpb;
}

static void load_pt(void) {
    int top = 0;
    memset(&mod, 0, sizeof mod);
    mod.header.songLength = d1[950];
    for (int i = 0; i < 128; i++) { mod.header.patternTable[i] = d1[952 + i]; if (d1[952 + i] > top) top = d1[952 + i]; }
    for (int p = 0; p <= top && p < MAX_PATTERNS; p++) {
        mod.patterns[p] = malloc(sizeof(note_t) * 256);
        for (int i = 0; i < 256; i++) {
            unsigned char *b = d1 + 1084 + p * 1024 + i * 4;
            note_t *n = &mod.patterns[p][i];
            n->period = (b[0] & 15) << 8 | b[1];
            n->sample = (b[0] & 0xF0) | b[2] >> 4;
            n->command = b[2] & 15;
            n->param = b[3];
        }
    }
    int off = 1084 + (top + 1) * 1024;
    mod.sampleData = (int8_t *)d1;
    for (int i = 0; i < 31; i++) {
        unsigned char *h = d1 + 20 + i * 30;
        moduleSample_t *s = &mod.samples[i];
        // те же границы, что в gs_pt_load
        long len = (h[22] << 8 | h[23]) * 2, ls = (h[26] << 8 | h[27]) * 2, ll = (h[28] << 8 | h[29]) * 2;
        if (off + len > size) len = (size - off) & ~1L;
        if (len < 0) len = 0;
        if (ll < 2) ll = 2;
        if (ls >= len) ls = 0, ll = 2;
        else if (ls + ll > len) ll = (len - ls) < 2 ? 2 : len - ls;
        if (len < 2) ls = 0, ll = 2;
        s->length = len, s->loopStart = ls, s->loopLength = ll;
        s->fineTune = h[24] & 15;
        s->volume = h[25] > 64 ? 64 : h[25];
        s->offset = off;
        off += len;
    }
}

static long off1(const int8_t *p) { return (!p || p == null_smp) ? -1 : (long)(p - (int8_t *)d1); }
static long off2(const int8_t *p) { return !p ? -1 : (long)(p - (int8_t *)d2); }

static int quiet;
static long run(const char *name, int max_ticks, int show) {
    d2 = malloc(size + 64); memcpy(d2, d1, size);
    load_pt();
    if (!gs_pt_load(d2, size, 1)) { printf("%s: не загрузился\n", name); return -1; }
    mod.speed = 6, mod.loaded = true;
    initializeModuleChannels(&mod);
    modSetTempo(125, false);
    editor.playMode = PLAY_MODE_NORMAL;
    modPlay(-1, 0, 0);
    mod.tick = mod.speed - 1;
    memset(reg_per, 0, sizeof reg_per); memset(reg_vol, 0, sizeof reg_vol); memset(reg_trig, 0, sizeof reg_trig); memset(my_trig, 0, sizeof my_trig);
    memset(reg_len, 0, sizeof reg_len); memset(reg_lc, 0, sizeof reg_lc);
    long bad[9] = {0}, trigs = 0; int printed = 0;
    static const char *nm[9] = {"ход","скорость","темп","период","громкость","старт","LC","LEN","старт LC/LEN/PER"};
    for (int t = 0; t < max_ticks; t++) {
        tickReplayer();
        gs_pt_tick();
        int e[9] = {0};
        e[0] = ptcmp_pos() != gs_pt_pos() || mod.row != gs_pt_row();
        e[1] = mod.speed != gs_pt_speed();
        e[2] = ptcmp_bpm() != gs_pt_bpm();
        for (int c = 0; c < 4; c++) {
            int e3 = reg_per[c] != gs_pt_reg[c].period, e4 = reg_vol[c] != (gs_pt_reg[c].vol & 127), e5 = reg_trig[c] != my_trig[c];
            int e6 = off1(reg_lc[c]) != off2(gs_pt_reg[c].lc), e7 = reg_len[c] != gs_pt_reg[c].len;
            int e8 = reg_trig[c] && (off1(trig_lc[0][c]) != off2(trig_lc[1][c]) || trig_len[0][c] != trig_len[1][c] || trig_per[0][c] != trig_per[1][c]);
            trigs += reg_trig[c];
            e[3] += e3, e[4] += e4, e[5] += e5, e[6] += e6, e[7] += e7, e[8] += e8;
            if ((e3 || e4 || e5 || e6 || e7 || e8) && printed < show) {
                printed++;
                printf("  т%d поз %d стр %d тик %d кан %d: пер %d/%d гр %d/%d старт %d/%d LC %ld/%ld LEN %d/%d\n", t, ptcmp_pos(), mod.row, mod.tick, c, reg_per[c], gs_pt_reg[c].period, reg_vol[c], gs_pt_reg[c].vol, reg_trig[c], my_trig[c], off1(reg_lc[c]), off2(gs_pt_reg[c].lc), reg_len[c], gs_pt_reg[c].len);
            }
            reg_trig[c] = my_trig[c] = 0;
        }
        for (int i = 0; i < 9; i++) bad[i] += e[i];
    }
    long sum = 0;
    if (!quiet) printf("%-22s тиков %d, стартов %ld:", name, max_ticks, trigs);
    for (int i = 0; i < 9; i++) { sum += bad[i]; if (bad[i] && !quiet) printf(" %s %ld", nm[i], bad[i]); }
    if (memcmp(d1, d2, size)) { if (!quiet) printf(" ДАННЫЕ СЭМПЛОВ РАЗНЫЕ"); sum++; }
    printf(sum ? "\n" : " всё совпало\n");
    free(d2);
    return sum;
}

int main(int argc, char **argv) {
    SetConsoleOutputCP(CP_UTF8);
    long sum = 0, stress_ok = 0;
    for (int i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        if (!f) { printf("нет %s\n", argv[i]); continue; }
        fseek(f, 0, SEEK_END); size = ftell(f); fseek(f, 0, SEEK_SET);
        d1 = malloc(size + 64); fread(d1, 1, size, f); fclose(f);
        const char *n = strrchr(argv[i], '/'); sum += run(n ? n + 1 : argv[i], 100000, 6);
    }
    for (unsigned s = 1; s <= 200; s++) {
        make_stress(s);
        char nm[32]; sprintf(nm, "стресс %u", s);
        quiet = 1; long r = run(nm, 20000, 0); quiet = 0;
        if (r) { make_stress(s); r = run(nm, 20000, 12); }
        else stress_ok++;
        sum += r;
        free(d1);
        if (r && s > 3) break;
    }
    printf("стресс без расхождений: %ld\nИТОГО расхождений: %ld\n", stress_ok, sum);
    return sum != 0;
}

// Заглушки экрана и звука
void updateCursorPos(void) {}
void lockAudio(void) {}
void unlockAudio(void) {}
void setVisualsDataPtr(int c, const int8_t *p) { (void)c, (void)p; }
void setVisualsLength(int c, uint16_t v) { (void)c, (void)v; }
void setVisualsDMACON(uint16_t v) { (void)v; }
void setVisualsVolume(int c, uint16_t v) { (void)c, (void)v; }
void setVisualsPeriod(int c, uint16_t v) { (void)c, (void)v; }
void resetAudioDither(void) {}
void pointerSetModeThreadSafe(int m, bool b) { (void)m, (void)b; }
void pointerSetMode(int m, bool b) { (void)m, (void)b; }
void setSyncTickTimeLen(uint32_t a, uint64_t b) { (void)a, (void)b; }
void posEdClearNames(void) {}
void setLEDFilter(bool b) { (void)b; }
void updateCurrSample(void) {}
void renderMuteButtons(void) {}
