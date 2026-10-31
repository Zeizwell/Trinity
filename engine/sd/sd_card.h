// SD по SPI: протокол без железа. Железо - таблица порта: на RP SPI1 + DMA, на ПК модель карты (tools/sdtest)
#ifndef SD_CARD_H
#define SD_CARD_H

#include <stdint.h>

typedef struct {
    void (*cs)(int on);                                 // 1 - карта выбрана
    void (*speed)(int fast);                            // 0 - до 400 кГц, 1 - рабочая
    uint8_t (*xfer)(uint8_t v);                         // один байт туда и обратно
    void (*rx)(uint8_t *p, int n);                      // блок: шлём FF
    void (*tx)(const uint8_t *p, int n);                // блок: ответ не нужен
    uint32_t (*now_ms)(void);
} sd_port_t;

void sd_set_port(const sd_port_t *port);
int sd_init(void);                                      // 1 - карта готова, 0 - нет или не отвечает
int sd_ready(void);
uint32_t sd_sectors(void);                              // размер карты, секторов по 512
int sd_read(uint32_t sector, uint8_t *buf, uint32_t count);
int sd_write(uint32_t sector, const uint8_t *buf, uint32_t count);
int sd_sync(void);                                      // дождаться конца записи

#endif
