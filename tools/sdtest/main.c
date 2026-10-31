// sdtest: SD и FAT на ПК. Модель карты понимает SPI по байтам и ругается на нарушения порядка, диск - файл-образ
// sdtest <новый образ> [образ чужой, только чтение: печатает корень]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "../../engine/sd/sd_card.h"
#include "../../engine/third_party/fatfs/ff.h"

static int fails;
static void check(const char *what, long got, long want) {
    int ok = got == want;
    printf("%-44s %ld (надо %ld) %s\n", what, got, want, ok ? "ok" : "ОШИБКА");
    if (!ok) fails++;
}

// --- модель карты ---
static FILE *img;
static int m_sdhc, m_v1, m_ronly, m_absent, m_badtoken;     // вид карты и поломки
static uint32_t m_sectors, m_busy_rd, m_busy_wr;            // задержки: байт FF перед токеном, байт занятости после записи
static int m_cs, m_fast, m_clocks_idle, m_spi, m_idle, m_cmd8, m_app, m_acmd41_left, m_viol;
static uint8_t m_cmd[6];
static int m_cmdn;
static enum { S_CMD, S_OUT, S_RDN, S_WR1, S_WRN, S_WRDATA, S_WRCRC, S_BUSY, S_SKIP } m_state;
static uint8_t m_out[64 + 520];
static int m_outp, m_outn, m_wait, m_wrn_mode, m_datan;
static uint32_t m_addr;
static uint8_t m_data[512];
static uint32_t m_rdcount, m_wrcount, m_stuck;
static uint32_t ms_fake;

static void viol(const char *s) { printf("  модель: нарушение - %s\n", s); m_viol++; }

static void sec_read(uint32_t s, uint8_t *p) { _fseeki64(img, (__int64)s * 512, SEEK_SET); memset(p, 0, 512); fread(p, 1, 512, img); }
static void sec_write(uint32_t s, const uint8_t *p) { _fseeki64(img, (__int64)s * 512, SEEK_SET); fwrite(p, 1, 512, img); }

static void out_reset(void) { m_outp = m_outn = 0; }
static void out_b(uint8_t v) { m_out[m_outn++] = v; }

static void out_block(void) {                               // токен + сектор + CRC в очередь
    m_wait = (int)m_busy_rd;
    out_b(m_badtoken ? 0x09 : 0xFE);
    sec_read(m_addr, m_out + m_outn);
    m_outn += 512;
    out_b(0), out_b(0);
    m_rdcount++;
}

static uint32_t sector_of(uint32_t arg) {
    if (m_sdhc) return arg;
    if (arg % 512) viol("SDSC: адрес не кратен 512");
    return arg / 512;
}

static void do_command(void) {
    uint8_t c = m_cmd[0] & 0x3F;
    uint32_t arg = (uint32_t)m_cmd[1] << 24 | m_cmd[2] << 16 | m_cmd[3] << 8 | m_cmd[4];
    int app = m_app;
    m_app = 0;
    out_reset();
    out_b(0xFF);                                            // Ncr
    m_state = S_OUT;
    if (!m_spi) {
        if (c != 0) { viol("до CMD0"); out_b(0xFF); return; }
        if (m_cmd[5] != 0x95) { viol("CMD0: плохой CRC"); return; }
        if (m_clocks_idle < 74) viol("меньше 74 тактов перед CMD0");
        if (m_fast) viol("инициализация не на низкой скорости");
        m_spi = 1, m_idle = 1;
        out_b(0x01);
        return;
    }
    if (m_idle && m_fast) viol("быстро до конца инициализации");
    switch (app ? 100 + c : c) {
    case 0: m_idle = 1, m_cmd8 = 0; out_b(0x01); break;
    case 8:
        if (m_v1) { out_b(0x05); break; }
        if (m_cmd[5] != 0x87) viol("CMD8: плохой CRC");
        m_cmd8 = 1;
        out_b(m_idle), out_b(0), out_b(0), out_b(1), out_b(arg & 255);
        break;
    case 55: m_app = 1; out_b(m_idle); break;
    case 141:
        if (!m_v1 && !m_cmd8) viol("ACMD41 без CMD8");
        if (m_sdhc && !(arg & 0x40000000u)) { out_b(0x01); break; }      // без HCS SDHC не проснётся
        if (m_acmd41_left) m_acmd41_left--, out_b(0x01);
        else m_idle = 0, out_b(0);
        break;
    case 58: out_b(m_idle), out_b(m_sdhc ? 0xC0 : 0x80), out_b(0xFF), out_b(0x80), out_b(0); break;
    case 16:
        if (arg != 512) viol("длина блока не 512");
        out_b(0);
        break;
    case 9: {
        out_b(0), out_b(0xFE);
        uint8_t csd[16] = {0};
        if (m_sdhc) {
            uint32_t cs = m_sectors / 1024 - 1;
            csd[0] = 0x40, csd[7] = (uint8_t)(cs >> 16), csd[8] = (uint8_t)(cs >> 8), csd[9] = (uint8_t)cs;
        } else {                                            // 512 байт, MULT 7 (x512): C_SIZE = sectors/512 - 1
            uint32_t cs = m_sectors / 512 - 1;
            csd[5] = 9, csd[6] = (uint8_t)(cs >> 10 & 3), csd[7] = (uint8_t)(cs >> 2), csd[8] = (uint8_t)(cs << 6), csd[9] = 3, csd[10] = 0x80;
        }
        memcpy(m_out + m_outn, csd, 16); m_outn += 16;
        out_b(0), out_b(0);
        break;
    }
    case 17: case 18:
        if (m_idle) viol("чтение до конца инициализации");
        m_addr = sector_of(arg);
        out_b(0);
        out_block();
        if (c == 18) m_state = S_RDN;
        break;
    case 24: case 25:
        if (m_idle) viol("запись до конца инициализации");
        if (m_ronly) { out_b(0x04); break; }
        m_addr = sector_of(arg);
        out_b(0);
        m_wrn_mode = c == 25;
        m_state = c == 25 ? S_WRN : S_WR1;
        break;
    case 12: out_b(0xFF), out_b(0); m_state = S_OUT; break;
    default: viol("неизвестная команда"); out_b(0x04); break;
    }
}

static uint8_t card_byte(uint8_t in) {
    if (m_absent) return 0xFF;
    if (!m_cs) {                                            // CS снят: считаем такты, команды нет
        m_clocks_idle += 8;
        return 0xFF;
    }
    // Команда принимается и посреди потока чтения (STOP)
    if (m_cmdn == 0 && (in & 0xC0) == 0x40 && (m_state == S_CMD || m_state == S_RDN || (m_state == S_OUT && m_outp >= m_outn))) {
        m_cmd[m_cmdn++] = in;
        return 0xFF;
    }
    if (m_cmdn) {
        m_cmd[m_cmdn++] = in;
        if (m_cmdn == 6) { m_cmdn = 0; do_command(); }
        return 0xFF;
    }
    switch (m_state) {
    case S_OUT: case S_RDN:
        if (in != 0xFF) viol("приём: на MOSI не FF");
        if (m_state == S_RDN && m_outp >= m_outn) { m_addr++; out_reset(); out_block(); }
        if (m_wait > 0 && m_outp < m_outn && m_out[m_outp] == 0xFE) { m_wait--; return 0xFF; }
        if (m_outp < m_outn) return m_out[m_outp++];
        m_state = S_CMD;
        return 0xFF;
    case S_WR1: case S_WRN:
        if (m_outp < m_outn) return m_out[m_outp++];
        if (in == 0xFF) return 0xFF;
        if (in == 0xFD) {
            if (m_state != S_WRN) viol("STOP не в потоке");
            m_stuck = m_busy_wr;
            m_wrn_mode = 0;
            m_state = S_BUSY;
            return 0xFF;
        }
        if (in != (m_state == S_WRN ? 0xFC : 0xFE)) viol("плохой токен записи");
        m_datan = 0;
        m_state = S_WRDATA;
        return 0xFF;
    case S_WRDATA:
        m_data[m_datan++] = in;
        if (m_datan == 512) { m_datan = 0; m_state = S_WRCRC; }
        return 0xFF;
    case S_WRCRC:
        if (++m_datan == 2) {
            sec_write(m_addr++, m_data);
            m_wrcount++;
            m_stuck = m_busy_wr;
            m_state = S_SKIP;
        }
        return 0xFF;
    case S_SKIP: m_state = S_BUSY; return 0xE5;             // принято
    case S_BUSY:
        if (m_stuck) { m_stuck--; return 0; }
        m_state = m_wrn_mode ? S_WRN : S_CMD;
        return 0xFF;
    default:
        return 0xFF;
    }
}

static void card_reset(int sdhc, int v1, uint32_t sectors) {
    m_sdhc = sdhc, m_v1 = v1, m_sectors = sectors;
    m_cs = m_fast = m_clocks_idle = m_spi = m_idle = m_cmd8 = m_app = m_viol = 0;
    m_cmdn = 0, m_state = S_CMD, m_acmd41_left = 20;
    m_absent = m_ronly = m_badtoken = 0;
    m_busy_rd = 50, m_busy_wr = 200;
    m_rdcount = m_wrcount = 0;
    out_reset();
}

// --- порт в модель ---
static void p_cs(int on) {
    if (!on && m_wrn_mode) viol("CS снят в потоке записи");
    if (!on && m_state != S_BUSY) m_state = S_CMD;          // занятая карта остаётся занятой и без CS
    m_cs = on;
}
static void p_speed(int fast) { m_fast = fast; }
static uint8_t p_xfer(uint8_t v) { ms_fake += m_absent; return card_byte(v); }
static void p_rx(uint8_t *p, int n) { while (n--) *p++ = card_byte(0xFF); }
static void p_tx(const uint8_t *p, int n) { while (n--) card_byte(*p++); }
static uint32_t p_now(void) { return ms_fake += 1; }
static const sd_port_t port = { p_cs, p_speed, p_xfer, p_rx, p_tx, p_now };

// --- тесты ---
static uint8_t pat[1 << 20], back[1 << 20];

static void raw_test(const char *name) {
    uint8_t w[512 * 8], r[512 * 8];
    for (int i = 0; i < (int)sizeof w; i++) w[i] = (uint8_t)(i * 13 + name[0]);
    printf("--- %s\n", name);
    check("инициализация", sd_init(), 1);
    check("размер, секторов", (long)sd_sectors(), (long)m_sectors);
    check("запись 1 сектора", sd_write(100, w, 1), 1);
    check("запись 8 секторов", sd_write(200, w, 8), 1);
    check("синхронизация", sd_sync(), 1);
    check("чтение 1 сектора", sd_read(100, r, 1), 1);
    check("  совпало", memcmp(r, w, 512), 0);
    memset(r, 0, sizeof r);
    check("чтение 8 секторов", sd_read(200, r, 8), 1);
    check("  совпало", memcmp(r, w, sizeof r), 0);
    check("нарушений протокола", m_viol, 0);
}

static void fat_test(BYTE fmt, int type, const char *name) {
    FATFS fs;
    FIL f;
    DIR d;
    FILINFO fi;
    UINT n;
    static uint8_t work[FF_MAX_SS * 8];
    MKFS_PARM parm = { fmt, 0, 0, 0, 0 };
    // Имя с русскими буквами в CP866, длинное
    const char *lname = "0:/TRINITY/\x8C\xAE\xA4\xE3\xAB\xEC \xE1 \xA4\xAB\xA8\xAD\xAD\xEB\xAC \xA8\xAC\xA5\xAD\xA5\xAC.mod";

    printf("--- %s\n", name);
    for (int i = 0; i < (int)sizeof pat; i++) pat[i] = (uint8_t)(i ^ i >> 8 ^ i >> 16);
    check("разметка", f_mkfs("0:", &parm, work, sizeof work), FR_OK);
    check("монтирование", f_mount(&fs, "0:", 1), FR_OK);
    check("тип файловой системы", fs.fs_type, type);
    check("каталог", f_mkdir("0:/TRINITY"), FR_OK);
    check("создать файл", f_open(&f, lname, FA_WRITE | FA_CREATE_ALWAYS), FR_OK);
    check("записать 1 МБ", f_write(&f, pat, sizeof pat, &n), FR_OK);
    check("  записано", n, sizeof pat);
    check("закрыть", f_close(&f), FR_OK);
    check("файл прошивки", f_open(&f, "0:/TRINITY/RPGS.BIN", FA_WRITE | FA_CREATE_ALWAYS), FR_OK);
    check("  запись", f_write(&f, pat, 4096, &n), FR_OK);
    f_close(&f);
    check("снять", f_mount(NULL, "0:", 0), FR_OK);

    check("смонтировать снова", f_mount(&fs, "0:", 1), FR_OK);
    check("открыть", f_open(&f, lname, FA_READ), FR_OK);
    check("размер", (long)f_size(&f), sizeof pat);
    memset(back, 0, sizeof back);
    check("прочитать", f_read(&f, back, sizeof back, &n), FR_OK);
    check("  совпало", memcmp(back, pat, sizeof pat), 0);
    check("смещение", f_lseek(&f, 700001), FR_OK);
    check("  чтение", f_read(&f, back, 1000, &n), FR_OK);
    check("  совпало", memcmp(back, pat + 700001, 1000), 0);
    f_close(&f);
    // Как автообновление: переименовать в .OLD, чтобы не шиться по кругу
    check("переименовать", f_rename("0:/TRINITY/RPGS.BIN", "0:/TRINITY/RPGS.OLD"), FR_OK);
    check("старого нет", f_stat("0:/TRINITY/RPGS.BIN", &fi), FR_NO_FILE);
    check("новый есть", f_stat("0:/TRINITY/RPGS.OLD", &fi), FR_OK);
    int cnt = 0;
    check("список", f_opendir(&d, "0:/TRINITY"), FR_OK);
    while (f_readdir(&d, &fi) == FR_OK && fi.fname[0]) cnt++;
    f_closedir(&d);
    check("  файлов", cnt, 2);
    check("нарушений протокола", m_viol, 0);
    f_mount(NULL, "0:", 0);
}

// Чужой образ: разметил не FatFs. Только чтение, модель запись отвергает
static void foreign(const char *path) {
    FATFS fs;
    DIR d;
    FILINFO fi;
    FILE *keep = img;
    img = fopen(path, "rb");
    if (!img) { img = keep; printf("нет %s\n", path); fails++; return; }
    _fseeki64(img, 0, SEEK_END);
    __int64 size = _ftelli64(img);
    card_reset(1, 0, (uint32_t)(size / 512));
    m_ronly = 1;
    printf("--- чужой образ %s, %lld МБ\n", path, size >> 20);
    check("монтирование", f_mount(&fs, "0:", 1), FR_OK);
    check("список", f_opendir(&d, "0:/"), FR_OK);
    int cnt = 0;
    while (f_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
        if (cnt++ < 12) printf("  %c %10lu %s\n", fi.fattrib & AM_DIR ? 'D' : ' ', (unsigned long)fi.fsize, fi.fname);
    }
    f_closedir(&d);
    check("нарушений протокола", m_viol, 0);
    f_mount(NULL, "0:", 0);
    fclose(img);
    img = keep;
}

int main(int argc, char **argv) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc < 2) { printf("sdtest <новый образ> [чужой образ]\n"); return 2; }
    img = fopen(argv[1], "w+b");
    if (!img) { printf("не создать %s\n", argv[1]); return 2; }
    _fseeki64(img, (__int64)64 * 1024 * 1024 - 1, SEEK_SET);
    fputc(0, img);
    sd_set_port(&port);

    card_reset(1, 0, 131072); raw_test("SDHC");
    card_reset(1, 0, 124735488); raw_test("SDXC 64 ГБ (размер из CSD, пишем в начало)");
    card_reset(0, 0, 131072); raw_test("SDSC v2");
    card_reset(0, 1, 131072); raw_test("SDSC v1");

    printf("--- поломки\n");
    card_reset(1, 0, 131072); m_absent = 1;
    check("нет карты", sd_init(), 0);
    { uint8_t b[512];
      check("чтение без карты", sd_read(0, b, 1), 0);
      card_reset(1, 0, 131072); sd_init(); m_badtoken = 1;
      check("плохой токен чтения", sd_read(0, b, 1), 0);
      card_reset(1, 0, 131072); sd_init(); m_ronly = 1;
      check("запись отвергнута", sd_write(0, b, 1), 0); }

    card_reset(1, 0, 131072);
    fat_test(FM_EXFAT, FS_EXFAT, "exFAT");
    card_reset(1, 0, 131072);
    fat_test(FM_FAT32, FS_FAT32, "FAT32");
    fflush(img);
    fclose(img);
    img = NULL;
    if (argc > 2) foreign(argv[2]);
    printf(fails ? "ОШИБОК: %d\n" : "ВСЁ OK\n", fails);
    return fails != 0;
}
