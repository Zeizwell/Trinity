#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "config.h"
#include "sd/sd_card.h"
#include "sdport.h"

#define SD_SPI      spi1
#define SD_SLOW_HZ  400000u
#define SD_FAST_HZ  25000000u           // SD Default Speed

static uint dma_tx, dma_rx;

static void p_cs(int on) { gpio_put(PIN_SD_CS, !on); }
static void p_speed(int fast) { spi_set_baudrate(SD_SPI, fast ? SD_FAST_HZ : SD_SLOW_HZ); }

static uint8_t p_xfer(uint8_t v) {
    uint8_t r;
    spi_write_read_blocking(SD_SPI, &v, &r, 1);
    return r;
}

// Блок: два канала DMA по DREQ SPI. Прием запускаем первым, иначе FIFO приёма может переполниться
static void run_dma(uint8_t *rx, const uint8_t *tx, int n) {
    static uint8_t dummy_tx = 0xFF, dummy_rx;
    dma_channel_config c = dma_channel_get_default_config(dma_rx);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, rx != NULL);
    channel_config_set_dreq(&c, spi_get_dreq(SD_SPI, false));
    dma_channel_configure(dma_rx, &c, rx ? rx : &dummy_rx, &spi_get_hw(SD_SPI)->dr, (uint)n, true);
    c = dma_channel_get_default_config(dma_tx);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, tx != NULL);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, spi_get_dreq(SD_SPI, true));
    dma_channel_configure(dma_tx, &c, &spi_get_hw(SD_SPI)->dr, tx ? tx : &dummy_tx, (uint)n, true);
    dma_channel_wait_for_finish_blocking(dma_rx);
}

static void p_rx(uint8_t *p, int n) { run_dma(p, NULL, n); }
static void p_tx(const uint8_t *p, int n) { run_dma(NULL, p, n); }
static uint32_t p_now(void) { return to_ms_since_boot(get_absolute_time()); }

static const sd_port_t port = { p_cs, p_speed, p_xfer, p_rx, p_tx, p_now };

void sdport_init(void) {
    spi_init(SD_SPI, SD_SLOW_HZ);
    gpio_set_function(PIN_SD_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SD_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SD_MOSI, GPIO_FUNC_SPI);
    gpio_pull_up(PIN_SD_MISO);
    gpio_init(PIN_SD_CS);
    gpio_set_dir(PIN_SD_CS, GPIO_OUT);
    gpio_put(PIN_SD_CS, 1);
    gpio_init(PIN_SD_DET);
    gpio_pull_up(PIN_SD_DET);
    dma_tx = (uint)dma_claim_unused_channel(true);
    dma_rx = (uint)dma_claim_unused_channel(true);
    sd_set_port(&port);
}

int sdport_present(void) { return !gpio_get(PIN_SD_DET); }
