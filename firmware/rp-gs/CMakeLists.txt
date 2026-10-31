# Прошивка RP-GS. Звук - ядро 1, шина и команды - ядро 0, замеры - по клавишам (bench.c).
# Сборка - build.bat файл_модуля (модули в git не кладём, путь снаружи).
cmake_minimum_required(VERSION 3.13)

set(PICO_PLATFORM rp2350-arm-s)
set(PICO_BOARD spotpear_rp2350b_mini)
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/../rp-ay/boards)

include(${CMAKE_CURRENT_LIST_DIR}/../rp-ay/pico_sdk_import.cmake)
project(rp_gs C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

set(E ${CMAKE_CURRENT_LIST_DIR}/../../engine/gs)
add_executable(rp_gs main.c psram.c audio.c bus.c cfgstore.c bench.c
    ${E}/gs_cfg.c ${E}/gs_chan.c ${E}/gs_mod.c ${E}/gs_core.c ${E}/gs_dev_gs.c ${E}/gs_dev_opl4.c
    ${E}/gs_fx.c ${E}/gs_blep.c ${E}/gs_filter.c ${E}/gs_down.c)
target_compile_definitions(rp_gs PRIVATE TEST_MOD="${TEST_MOD}")
target_include_directories(rp_gs PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../engine)
target_compile_options(rp_gs PRIVATE -O3)
pico_generate_pio_header(rp_gs ${CMAKE_CURRENT_LIST_DIR}/i2s.pio)
pico_generate_pio_header(rp_gs ${CMAKE_CURRENT_LIST_DIR}/../rp-ay/bus.pio)
target_link_libraries(rp_gs pico_stdlib pico_multicore hardware_clocks hardware_pio hardware_dma hardware_vreg hardware_flash)
pico_enable_stdio_usb(rp_gs 1)
pico_enable_stdio_uart(rp_gs 0)
pico_set_binary_type(rp_gs copy_to_ram)     # п.44: только из ОЗУ
pico_add_extra_outputs(rp_gs)
