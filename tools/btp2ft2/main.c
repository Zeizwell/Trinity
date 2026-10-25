// btp2ft2 - Bitphase (.btp) -> FT2 (.ft2) на ПК. Тот же конвертер, что будет сервисом карты (engine/btp).
//   btp2ft2 вход.btp выход.ft2   -> лог: что потеряно / приближено
#include <stdio.h>
#include <stdlib.h>
#include "../../engine/btp/btp2ft2.h"

static btp_conv_t conv;
static FILE *fo;

static void on_out(void *u, const uint8_t *p, uint32_t n) { fwrite(p, 1, n, fo); }
static void on_log(void *u, const char *t) { printf("  %s\n", t); }
static void on_chunk(void *u, const uint8_t *p, uint32_t n) { btp_feed(&conv, p, n); }

int main(int argc, char **argv) {
    if (argc < 3) { printf("btp2ft2 in.btp out.ft2\n"); return 1; }
    FILE *fi = fopen(argv[1], "rb");
    if (!fi) { printf("no %s\n", argv[1]); return 1; }
    fseek(fi, 0, SEEK_END);
    long len = ftell(fi);
    fseek(fi, 0, SEEK_SET);
    uint8_t *src = malloc(len);
    fread(src, 1, len, fi);
    fclose(fi);
    fo = fopen(argv[2], "wb");
    btp_init(&conv, on_out, on_log, NULL);
    for (int pass = 1; pass <= 2; pass++) {             // вход - дважды (btp2ft2.h)
        btp_pass(&conv, pass);
        if (!btp_gunzip(src, (uint32_t)len, on_chunk, NULL)) { printf("gzip: error\n"); return 2; }
        if (conv.js.error) { printf("json: error (pass %d)\n", pass); return 3; }
    }
    bool ok = btp_finish(&conv);
    fclose(fo);
    printf("%s -> %s\n", argv[1], argv[2]);
    return ok ? 0 : 4;
}
