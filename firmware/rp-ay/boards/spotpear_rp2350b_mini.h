// Spotpear RP2350B-MINI-A (RP2350B, 48 GPIO). Схема - FT2/schemes/spotpear/.
// Занято на плате: GP19 - CS PSRAM, GP20 - RGB (WS2812), GP24 - VBUS USB.
// GP40..GP47 - АЦП, 5 В не терпят.
#ifndef _BOARDS_SPOTPEAR_RP2350B_MINI_H
#define _BOARDS_SPOTPEAR_RP2350B_MINI_H

// pico_cmake_set PICO_PLATFORM=rp2350

#define SPOTPEAR_RP2350B_MINI
#define PICO_RP2350A 0              // RP2350B: 48 GPIO

#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1
#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 4    // RP на 250 МГц: флеш 62.5 МГц, с запасом
#endif
// pico_cmake_set_default PICO_FLASH_SIZE_BYTES = (16 * 1024 * 1024)
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (16 * 1024 * 1024)  // флеш 16 МБ, PSRAM 8 МБ (пользователь, 2026-11-03)
#endif
#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

#endif
