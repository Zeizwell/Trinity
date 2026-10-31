// gsplay - движок RP-GS на ПК: модуль в колонки или в WAV. Тот же код, что на RP (engine/gs), HAL здесь - waveOut.
//   gsplay файл.mod             - играть. Клавиши: 1-4 режим, пробел пауза, s стоп / с начала, q выход
//   gsplay файл.mod -wav x.wav [сек] [режим 1-4]   - без звука, в файл
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <mmsystem.h>
#include <conio.h>
#include "../../engine/gs/gs_core.h"
#include "../../engine/gs/gs_cfg.h"
#include "../../engine/gs/gs_dev_gs.h"

#define BLOCK 2048              // отсчётов в блоке
#define BLOCKS 4

static unsigned char *load(const char *name, int *size) {
    FILE *f = fopen(name, "rb");
    if (!f) { printf("no %s\n", name); exit(1); }
    fseek(f, 0, SEEK_END);
    *size = (int)ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc((size_t)*size);
    fread(b, 1, (size_t)*size, f);
    fclose(f);
    return b;
}

static void to_pcm(const float *in, short *out, int n) {
    for (int i = 0; i < n * 2; i++) out[i] = (short)(in[i] * 32767.0f);
}

static void put32(FILE *f, unsigned v) { fwrite(&v, 4, 1, f); }
static void put16(FILE *f, unsigned v) { fwrite(&v, 2, 1, f); }

static int to_wav(const char *name, int secs) {
    FILE *f = fopen(name, "wb");
    if (!f) { printf("can't write %s\n", name); return 1; }
    unsigned rate = gs_cfg.rate_hz, frames = rate * (unsigned)secs, bytes = frames * 4;
    fwrite("RIFF", 4, 1, f), put32(f, 36 + bytes), fwrite("WAVEfmt ", 8, 1, f), put32(f, 16);
    put16(f, 1), put16(f, 2), put32(f, rate), put32(f, rate * 4), put16(f, 4), put16(f, 16);
    fwrite("data", 4, 1, f), put32(f, bytes);
    static float fb[BLOCK * 2];
    static short pcm[BLOCK * 2];
    for (unsigned done = 0; done < frames; done += BLOCK) {
        int n = frames - done < BLOCK ? (int)(frames - done) : BLOCK;
        gs_core_audio(fb, n);
        to_pcm(fb, pcm, n);
        fwrite(pcm, 4, (size_t)n, f);
    }
    fclose(f);
    printf("%s: %d s, mode %s\n", name, secs, gs_cfg_mode_name(gs_cfg.mode));
    return 0;
}

static void show(int paused) {
    printf("\rmode %-12s PWM %-3s %-7s   ", gs_cfg_mode_name(gs_cfg.mode), gs_cfg.pwm ? "on" : "off", paused ? "pause" : "play");
    fflush(stdout);
}

static int live(const unsigned char *mod, int size) {
    WAVEFORMATEX wf = { WAVE_FORMAT_PCM, 2, gs_cfg.rate_hz, gs_cfg.rate_hz * 4, 4, 16, 0 };
    HWAVEOUT wo;
    HANDLE ev = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (waveOutOpen(&wo, WAVE_MAPPER, &wf, (DWORD_PTR)ev, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        printf("no audio device\n");
        return 1;
    }
    static short pcm[BLOCKS][BLOCK * 2];
    static float fb[BLOCK * 2];
    static WAVEHDR hdr[BLOCKS];
    for (int i = 0; i < BLOCKS; i++) {
        hdr[i].lpData = (LPSTR)pcm[i];
        hdr[i].dwBufferLength = BLOCK * 4;
        waveOutPrepareHeader(wo, &hdr[i], sizeof hdr[i]);
        hdr[i].dwFlags |= WHDR_DONE;
    }
    printf("1 GS  2 Paula A500  3 Paula A1200  4 HiFi  |  p PWM on/off  space pause  s stop/start  q quit\n");
    int paused = 0, run = 1;
    show(paused);
    for (;;) {
        while (_kbhit()) {
            int k = _getch();
            if (k == 'q' || k == 27) goto done;
            if (k >= '1' && k <= '4') gs_cfg_mode((gs_mode_t)(k - '1'));
            if (k == 'p') gs_cfg.pwm = !gs_cfg.pwm;
            if (k == ' ') paused = !paused;
            if (k == 's') {
                run = !run;
                if (run) gs_gs_play_mod(mod, size); else gs_gs_stop();
            }
            show(paused);
        }
        int filled = 0;
        for (int i = 0; i < BLOCKS; i++) {
            if (!(hdr[i].dwFlags & WHDR_DONE)) continue;
            if (paused) memset(fb, 0, sizeof fb); else gs_core_audio(fb, BLOCK);
            to_pcm(fb, pcm[i], BLOCK);
            waveOutWrite(wo, &hdr[i], sizeof hdr[i]);
            filled = 1;
        }
        if (!filled) WaitForSingleObject(ev, 20);
    }
done:
    waveOutReset(wo);
    for (int i = 0; i < BLOCKS; i++) waveOutUnprepareHeader(wo, &hdr[i], sizeof hdr[i]);
    waveOutClose(wo);
    printf("\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) { printf("gsplay file.mod [-wav out.wav [secs [mode 1-4]]]\n"); return 1; }
    int size;
    unsigned char *mod = load(argv[1], &size);
    gs_core_init();
    if (!gs_gs_play_mod(mod, size)) { printf("%s: not a MOD\n", argv[1]); return 1; }
    if (argc >= 4 && !strcmp(argv[2], "-wav")) {
        int secs = argc > 4 ? atoi(argv[4]) : 30;
        if (argc > 5) gs_cfg_mode((gs_mode_t)(atoi(argv[5]) - 1));
        return to_wav(argv[3], secs);
    }
    return live(mod, size);
}
