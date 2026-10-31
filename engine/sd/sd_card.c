#include "sd_card.h"

#define CMD_GO_IDLE     0
#define CMD_IF_COND     8
#define CMD_SEND_CSD    9
#define CMD_STOP        12
#define CMD_BLOCKLEN    16
#define CMD_READ1       17
#define CMD_READN       18
#define CMD_WRITE1      24
#define CMD_WRITEN      25
#define CMD_APP         55
#define CMD_READ_OCR    58
#define ACMD_OP_COND    41

#define R1_IDLE         0x01
#define TOKEN_DATA      0xFE
#define TOKEN_WRITEN    0xFC
#define TOKEN_STOP      0xFD

// Сроки из SD Physical Layer: инициализация до 1 с, чтение 100 мс, запись 250 мс (берём 500: SDXC)
#define INIT_MS         1000u
#define READ_MS         100u
#define WRITE_MS        500u

static const sd_port_t *sp;
static int ready, block_addr;       // block_addr: SDHC адресует секторами, SDSC байтами
static uint32_t sectors;

void sd_set_port(const sd_port_t *port) { sp = port; ready = 0; }
int sd_ready(void) { return ready; }
uint32_t sd_sectors(void) { return sectors; }

// Занята, пока держит 00. 1 - дождались
static int wait_free(uint32_t ms) {
    uint32_t t0 = sp->now_ms();
    while (sp->xfer(0xFF) != 0xFF)
        if (sp->now_ms() - t0 > ms) return 0;
    return 1;
}

static void deselect(void) {
    sp->cs(0);
    sp->xfer(0xFF);                 // ещё 8 тактов: карта отпускает MISO
}

// Команда: ждём готовность, CRC нужен только CMD0 и CMD8 (в SPI дальше не проверяется). Ответ R1; бит 7 = нет ответа
static uint8_t command(uint8_t cmd, uint32_t arg) {
    sp->cs(1);
    if (cmd != CMD_GO_IDLE && !wait_free(WRITE_MS)) return 0xFF;
    sp->xfer((uint8_t)(0x40 | cmd));
    sp->xfer((uint8_t)(arg >> 24));
    sp->xfer((uint8_t)(arg >> 16));
    sp->xfer((uint8_t)(arg >> 8));
    sp->xfer((uint8_t)arg);
    sp->xfer(cmd == CMD_GO_IDLE ? 0x95 : cmd == CMD_IF_COND ? 0x87 : 0x01);
    if (cmd == CMD_STOP) sp->xfer(0xFF);                // байт мусора сразу за STOP
    uint8_t r = 0xFF;
    for (int i = 0; i < 10 && (r & 0x80); i++) r = sp->xfer(0xFF);
    return r;
}

// Блок от карты: токен, данные, CRC (не проверяем)
static int rx_block(uint8_t *p, int n) {
    uint32_t t0 = sp->now_ms();
    uint8_t t;
    while ((t = sp->xfer(0xFF)) == 0xFF)
        if (sp->now_ms() - t0 > READ_MS) return 0;
    if (t != TOKEN_DATA) return 0;
    sp->rx(p, n);
    sp->xfer(0xFF);
    sp->xfer(0xFF);
    return 1;
}

// Блок в карту: токен, данные, CRC любой, ответ xxx00101 - принято
static int tx_block(const uint8_t *p, uint8_t token) {
    sp->xfer(token);
    sp->tx(p, 512);
    sp->xfer(0xFF);
    sp->xfer(0xFF);
    return (sp->xfer(0xFF) & 0x1F) == 0x05;
}

// Размер из CSD: v2 - C_SIZE 22 бита, v1 - C_SIZE, MULT, READ_BL_LEN
static uint32_t read_size(void) {
    uint8_t c[16];
    uint32_t n = 0;
    if (command(CMD_SEND_CSD, 0) == 0 && rx_block(c, 16)) {
        if (c[0] >> 6 == 1) {
            n = (((uint32_t)(c[7] & 0x3F) << 16 | (uint32_t)c[8] << 8 | c[9]) + 1) << 10;
        } else {
            uint32_t size = ((uint32_t)(c[6] & 3) << 10 | (uint32_t)c[7] << 2 | c[8] >> 6) + 1;
            uint32_t mult = (uint32_t)(c[9] & 3) << 1 | c[10] >> 7;
            n = size << (mult + 2 + (c[5] & 15) - 9);
        }
    }
    deselect();
    return n;
}

int sd_init(void) {
    uint8_t r7[4], ocr[4];
    ready = 0, sectors = 0;
    if (!sp) return 0;
    // Шаг 1: медленно, CS снят, не меньше 74 тактов
    sp->speed(0);
    sp->cs(0);
    for (int i = 0; i < 10; i++) sp->xfer(0xFF);
    // Шаг 2: в режим SPI. Карта может быть посреди старого обмена - несколько попыток
    uint8_t r = 0xFF;
    for (int i = 0; i < 16 && r != R1_IDLE; i++) {
        r = command(CMD_GO_IDLE, 0);
        deselect();
    }
    if (r != R1_IDLE) return 0;
    // Шаг 3: CMD8 - карта v2 отвечает эхом. v1 считает её незаконной
    int v2 = 0;
    r = command(CMD_IF_COND, 0x1AA);
    if (r == R1_IDLE) {
        for (int i = 0; i < 4; i++) r7[i] = sp->xfer(0xFF);
        if (r7[2] != 0x01 || r7[3] != 0xAA) { deselect(); return 0; }
        v2 = 1;
    }
    deselect();
    // Шаг 4: ждём конца внутренней инициализации
    uint32_t t0 = sp->now_ms();
    for (;;) {
        command(CMD_APP, 0);
        deselect();
        r = command(ACMD_OP_COND, v2 ? 0x40000000u : 0);
        deselect();
        if (r == 0) break;
        if ((r & ~R1_IDLE) || sp->now_ms() - t0 > INIT_MS) return 0;
    }
    // Шаг 5: как адресует. SDSC - сектор принудительно 512
    block_addr = 0;
    if (v2) {
        if (command(CMD_READ_OCR, 0) != 0) { deselect(); return 0; }
        for (int i = 0; i < 4; i++) ocr[i] = sp->xfer(0xFF);
        block_addr = ocr[0] >> 6 & 1;
        deselect();
    }
    if (!block_addr) {
        r = command(CMD_BLOCKLEN, 512);
        deselect();
        if (r != 0) return 0;
    }
    // Шаг 6: рабочая скорость, размер
    sp->speed(1);
    sectors = read_size();
    ready = sectors != 0;
    return ready;
}

static uint32_t addr_of(uint32_t sector) { return block_addr ? sector : sector * 512u; }

int sd_read(uint32_t sector, uint8_t *buf, uint32_t count) {
    int ok = 1;
    if (!ready || !count) return 0;
    if (command(count == 1 ? CMD_READ1 : CMD_READN, addr_of(sector)) != 0) { deselect(); return 0; }
    for (uint32_t i = 0; i < count && ok; i++, buf += 512) ok = rx_block(buf, 512);
    if (count > 1) ok &= command(CMD_STOP, 0) == 0;
    deselect();
    return ok;
}

int sd_write(uint32_t sector, const uint8_t *buf, uint32_t count) {
    int ok = 1;
    if (!ready || !count) return 0;
    if (command(count == 1 ? CMD_WRITE1 : CMD_WRITEN, addr_of(sector)) != 0) { deselect(); return 0; }
    uint8_t token = count == 1 ? TOKEN_DATA : TOKEN_WRITEN;
    for (uint32_t i = 0; i < count && ok; i++, buf += 512) {
        ok = wait_free(WRITE_MS) && tx_block(buf, token);
    }
    if (count > 1) {
        ok &= wait_free(WRITE_MS);                      // конец потока, карта занята до конца записи
        sp->xfer(TOKEN_STOP);
        sp->xfer(0xFF);
    }
    ok &= wait_free(WRITE_MS);
    deselect();
    return ok;
}

int sd_sync(void) {
    if (!ready) return 0;
    sp->cs(1);
    int ok = wait_free(WRITE_MS);
    deselect();
    return ok;
}
