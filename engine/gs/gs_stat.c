#include "gs_stat.h"

#define WINDOW_US 500000u       // окно средней нагрузки

gs_stat_t gs_stat;

static volatile uint8_t reset_wanted;
static uint32_t sum_us, sum_budget;

void gs_stat_reset(void) { reset_wanted = 1; }

void gs_stat_late(void) { gs_stat.late++; }

void gs_stat_block(uint32_t us, uint32_t budget_us) {
    if (reset_wanted) {
        reset_wanted = 0;
        gs_stat.top_us = 0, gs_stat.top_x10 = 0, gs_stat.late = 0;
        sum_us = 0, sum_budget = 0;
    }
    gs_stat.last_us = us;
    gs_stat.blocks++;
    if (us > gs_stat.top_us) {
        gs_stat.top_us = us;
        gs_stat.top_x10 = (uint16_t)(us * 1000u / budget_us);
    }
    sum_us += us, sum_budget += budget_us;
    if (sum_budget >= WINDOW_US) {
        gs_stat.load_x10 = (uint16_t)((uint64_t)sum_us * 1000u / sum_budget);
        sum_us = 0, sum_budget = 0;
    }
}
