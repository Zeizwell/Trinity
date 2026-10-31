#include "pico/stdlib.h"
#include "hardware/structs/qmi.h"
#include "hardware/structs/xip_ctrl.h"
#include "hardware/sync.h"
#include "psram.h"

#define PIN_PSRAM_CS 19

// Сроки при 250 МГц, CLKDIV 3 = 83 МГц (APS6404: до 84 МГц на страницах 1 КБ)
#define CLKDIV     3
#define RXDELAY    3
#define MAX_SELECT 31           // 8 мкс (обновление чипа), в единицах 64 такта
#define MIN_DESEL  13            // 50 нс, в тактах

// Один байт команды, прямым режимом
static uint32_t direct(uint32_t tx) {
    qmi_hw->direct_tx = tx;
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) { }
    return qmi_hw->direct_rx;
}

static void cmd(uint32_t tx) {
    qmi_hw->direct_csr |= QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
    direct(tx);
    qmi_hw->direct_csr &= ~QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) { }
}

uint32_t psram_init(void) {
    uint32_t id = 0;
    gpio_set_function(PIN_PSRAM_CS, GPIO_FUNC_XIP_CS1);
    uint32_t ints = save_and_disable_interrupts();
    qmi_hw->direct_csr = 30 << QMI_DIRECT_CSR_CLKDIV_LSB | QMI_DIRECT_CSR_EN_BITS;
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) { }

    // Выйти из QPI, если чип уже в нём (тёплый старт)
    cmd(QMI_DIRECT_TX_OE_BITS | QMI_DIRECT_TX_IWIDTH_VALUE_Q << QMI_DIRECT_TX_IWIDTH_LSB | 0xF5);

    // ID: команда, 3 байта адреса, ответ
    qmi_hw->direct_csr |= QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
    direct(0x9F);
    for (int i = 0; i < 3; i++) direct(0xFF);
    for (int i = 0; i < 3; i++) id = id << 8 | (direct(0xFF) & 0xFF);
    qmi_hw->direct_csr &= ~QMI_DIRECT_CSR_ASSERT_CS1N_BITS;

    cmd(0x66);                  // сброс
    cmd(0x99);
    busy_wait_us(100);
    cmd(0x35);                  // в QPI

    qmi_hw->direct_csr &= ~QMI_DIRECT_CSR_EN_BITS;

    qmi_hw->m[1].timing = 1u << QMI_M1_TIMING_COOLDOWN_LSB
        | QMI_M1_TIMING_PAGEBREAK_VALUE_1024 << QMI_M1_TIMING_PAGEBREAK_LSB
        | MAX_SELECT << QMI_M1_TIMING_MAX_SELECT_LSB
        | MIN_DESEL << QMI_M1_TIMING_MIN_DESELECT_LSB
        | RXDELAY << QMI_M1_TIMING_RXDELAY_LSB
        | CLKDIV << QMI_M1_TIMING_CLKDIV_LSB;
    qmi_hw->m[1].rfmt = QMI_M1_RFMT_PREFIX_WIDTH_VALUE_Q << QMI_M1_RFMT_PREFIX_WIDTH_LSB
        | QMI_M1_RFMT_ADDR_WIDTH_VALUE_Q << QMI_M1_RFMT_ADDR_WIDTH_LSB
        | QMI_M1_RFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M1_RFMT_SUFFIX_WIDTH_LSB
        | QMI_M1_RFMT_DUMMY_WIDTH_VALUE_Q << QMI_M1_RFMT_DUMMY_WIDTH_LSB
        | QMI_M1_RFMT_DATA_WIDTH_VALUE_Q << QMI_M1_RFMT_DATA_WIDTH_LSB
        | QMI_M1_RFMT_PREFIX_LEN_VALUE_8 << QMI_M1_RFMT_PREFIX_LEN_LSB
        | 6u << QMI_M1_RFMT_DUMMY_LEN_LSB;      // 24 такта ожидания
    qmi_hw->m[1].rcmd = 0xEB;
    qmi_hw->m[1].wfmt = QMI_M1_WFMT_PREFIX_WIDTH_VALUE_Q << QMI_M1_WFMT_PREFIX_WIDTH_LSB
        | QMI_M1_WFMT_ADDR_WIDTH_VALUE_Q << QMI_M1_WFMT_ADDR_WIDTH_LSB
        | QMI_M1_WFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M1_WFMT_SUFFIX_WIDTH_LSB
        | QMI_M1_WFMT_DUMMY_WIDTH_VALUE_Q << QMI_M1_WFMT_DUMMY_WIDTH_LSB
        | QMI_M1_WFMT_DATA_WIDTH_VALUE_Q << QMI_M1_WFMT_DATA_WIDTH_LSB
        | QMI_M1_WFMT_PREFIX_LEN_VALUE_8 << QMI_M1_WFMT_PREFIX_LEN_LSB;
    qmi_hw->m[1].wcmd = 0x38;
    xip_ctrl_hw->ctrl |= XIP_CTRL_WRITABLE_M1_BITS;
    restore_interrupts(ints);
    return id;
}

uint32_t psram_test(uint32_t size) {
    volatile uint8_t *p = PSRAM_BASE;
    uint32_t bad = 0;
    for (uint32_t i = 0; i < size; i++) p[i] = (uint8_t)(i * 31u + (i >> 8));
    for (uint32_t i = 0; i < size; i++) if (p[i] != (uint8_t)(i * 31u + (i >> 8))) bad++;
    return bad;
}
