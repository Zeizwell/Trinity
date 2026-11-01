#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "gs/gs_core.h"
#include "gs/gs_cfg.h"
#include "config.h"
#include "audio.h"
#include "gs/gs_stat.h"
#include "i2s.pio.h"


#define FLOAT_TO_I32 2147483520.0f      // ближайшее к 2^31 снизу, что есть во float

static PIO pio = pio0;
static uint sm_out, sm_chk, off_chk;
static int dma_ch[2];

static int32_t dma_buf[2][AUDIO_FRAMES * 2] __attribute__((aligned(8)));
static float mix[AUDIO_FRAMES * 2];
static uint32_t pattern_n, rate_now;

static volatile uint8_t free_mask;      // какие буферы DMA уже отыграл

volatile int audio_pattern, audio_hold;

// Отыграл - адрес на начало, буфер отдаём ядру 1. Цепочка каналов идёт сама
static void __isr dma_irq(void) {
    uint32_t st = dma_hw->ints1;
    dma_hw->ints1 = st;
    for (int i = 0; i < 2; i++) {
        if (!(st >> dma_ch[i] & 1)) continue;
        dma_channel_set_read_addr(dma_ch[i], dma_buf[i], false);
        if (free_mask & (1u << i)) gs_stat_late();   // прошлый раз не успели посчитать
        free_mask |= (uint8_t)(1u << i);
    }
    __sev();
}

static void render(int i) {
    int32_t *out = dma_buf[i];
    if (audio_hold) {                               // движок занят замером на ядре 0
        memset(out, 0, sizeof dma_buf[i]);
        return;
    }
    if (audio_pattern) {
        for (int s = 0; s < AUDIO_FRAMES; s++, pattern_n++) {
            out[s * 2] = (int32_t)(0x11000000u | (pattern_n & 0xFFFFFF));
            out[s * 2 + 1] = (int32_t)(0x22000000u | (pattern_n & 0xFFFFFF));
        }
        return;
    }
    if (gs_cfg.rate_hz != rate_now) {               // режим сменил частоту ЦАП
        rate_now = gs_cfg.rate_hz;
        pio_sm_set_clkdiv(pio, sm_out, (float)clock_get_hz(clk_sys) / (float)(rate_now * 64u * 2u));
    }
    gs_core_lock();
    gs_core_audio(mix, AUDIO_FRAMES);
    gs_core_unlock();
    for (int s = 0; s < AUDIO_FRAMES * 2; s++) out[s] = (int32_t)(mix[s] * FLOAT_TO_I32);
}

static void core1_main(void) {
    multicore_lockout_victim_init();                // запись настроек во флеш останавливает это ядро
    irq_set_exclusive_handler(DMA_IRQ_1, dma_irq);
    irq_set_enabled(DMA_IRQ_1, true);       // на ядре 1
    dma_channel_start(dma_ch[0]);
    pio_sm_set_enabled(pio, sm_out, true);
    for (;;) {
        while (!free_mask) __wfe();
        int i = free_mask & 1 ? 0 : 1;
        uint32_t t = time_us_32();
        render(i);
        uint32_t d = time_us_32() - t;
        uint32_t ints = save_and_disable_interrupts();
        free_mask &= (uint8_t)~(1u << i);
        restore_interrupts(ints);
        gs_stat_block(d, AUDIO_FRAMES * 1000000u / gs_cfg.rate_hz);
    }
}

void audio_start(void) {
#ifdef PIN_XSMT
    pio_set_gpio_base(pio, 16);                     // I2S на GP40..42
    gpio_init(PIN_XSMT);
    gpio_set_dir(PIN_XSMT, GPIO_OUT);
    gpio_put(PIN_XSMT, 1);
#endif
    sm_out = pio_claim_unused_sm(pio, true);
    sm_chk = pio_claim_unused_sm(pio, true);
    uint off = pio_add_program(pio, &i2s_out_program);
    off_chk = pio_add_program(pio, &i2s_check_program);
    i2s_out_init(pio, sm_out, off, PIN_DIN, PIN_BCK, gs_cfg.rate_hz);
    rate_now = gs_cfg.rate_hz;
    dma_ch[0] = dma_claim_unused_channel(true);
    dma_ch[1] = dma_claim_unused_channel(true);
    for (int i = 0; i < 2; i++) {
        dma_channel_config c = dma_channel_get_default_config(dma_ch[i]);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        channel_config_set_read_increment(&c, true);
        channel_config_set_write_increment(&c, false);
        channel_config_set_dreq(&c, pio_get_dreq(pio, sm_out, true));
        channel_config_set_chain_to(&c, dma_ch[1 - i]);
        dma_channel_configure(dma_ch[i], &c, &pio->txf[sm_out], dma_buf[i], AUDIO_FRAMES * 2, false);
    }
    dma_hw->ints1 = 1u << dma_ch[0] | 1u << dma_ch[1];
    dma_set_irq1_channel_mask_enabled(1u << dma_ch[0] | 1u << dma_ch[1], true);
    multicore_launch_core1(core1_main);
}

int audio_check(uint32_t *words, int n) {
    pio_sm_set_enabled(pio, sm_chk, false);
    pio_sm_clear_fifos(pio, sm_chk);
    i2s_check_init(pio, sm_chk, off_chk, PIN_DIN);
    pio_sm_set_enabled(pio, sm_chk, true);
    uint32_t until = time_us_32() + 100000;
    int got = 0;
    while (got < n && (int32_t)(until - time_us_32()) > 0)
        if (!pio_sm_is_rx_fifo_empty(pio, sm_chk)) words[got++] = pio_sm_get(pio, sm_chk);
    pio_sm_set_enabled(pio, sm_chk, false);
    return got;
}
