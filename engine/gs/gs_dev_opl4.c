#include "gs_core.h"
#include "gs_dev_opl4.h"


static int  dev_enabled(void) { return 1; }
static void dev_reset(void) { }
static void dev_sound_stop(void) { }
static void dev_port_write(uint8_t code, uint8_t v) { (void)code, (void)v; }
static uint8_t dev_port_read(uint8_t code) { (void)code; return 0xFF; }
static void dev_work(void) { }
static void dev_audio(float *out, int n) { (void)out, (void)n; }

const gs_dev_t gs_dev_opl4 = {
    "OPL4", dev_enabled, dev_reset, dev_sound_stop, dev_port_write, dev_port_read, dev_port_read, dev_work, dev_audio
};
