# Прошивка RP-AY макета Тринити: RP в разрыве слота YM2203 на MultiSound.
# Сборка - build.bat (пути к SDK и инструментам - в нём).
cmake_minimum_required(VERSION 3.13)

set(PICO_PLATFORM rp2350-arm-s)       # RP2350, ядра ARM
set(PICO_BOARD spotpear_rp2350b_mini)
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)

include(pico_sdk_import.cmake)
project(rp_ay C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_executable(rp_ay main.c config.c modes.c chip.c card.c ../../engine/ayfx.c)   # движок AY FX - общий (engine/)
target_include_directories(rp_ay PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../engine)
pico_generate_pio_header(rp_ay ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio)
pico_generate_pio_header(rp_ay ${CMAKE_CURRENT_LIST_DIR}/bus.pio)
pico_generate_pio_header(rp_ay ${CMAKE_CURRENT_LIST_DIR}/chip_wr.pio)
target_link_libraries(rp_ay pico_stdlib pico_multicore hardware_pio hardware_gpio hardware_clocks)
pico_enable_stdio_usb(rp_ay 1)      # консоль - по USB (COM-порт)
pico_enable_stdio_uart(rp_ay 0)
pico_add_extra_outputs(rp_ay)       # rp_ay.uf2 - кинуть на диск RP (BOOT + USB)
