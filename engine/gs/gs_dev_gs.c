// Лицо GS: порты #BB / #B3 и команды. Протокол - FULGSCOM.TXT и main_ngs.a80 (ROM GS)
#include "gs_core.h"
#include "gs_cfg.h"
#include "gs_mod.h"
#include "gs_dev_gs.h"

const gs_dev_t gs_dev_gs;

#define PAGE_SIZE 32768u
#define VOL_MAX   0x40

// Регистры. Биты - отдельными байтами: их меняют и шина, и ядро 0
static volatile uint8_t reg_cmd, reg_in, reg_out;
static volatile uint8_t bit_cmd, bit_data;

// Память модулей. Поток пишет шина, пока stream = 1
static uint8_t *ram;
static uint32_t ram_size;
static volatile uint32_t ram_used;
static volatile uint8_t stream;

static uint32_t mod_size;
static uint8_t mod_count, mod_cur, mod_playing;     // один модуль: номер 1, 0 - нет
static uint8_t vol_master, vol_module;              // #2A, #35
static uint8_t notes_old[4], vols_old[4];           // для бита 7 в #63, #64

// Ответ из нескольких байт: следующий - когда Спектрум прочёл предыдущий
static uint8_t send_buf[4], send_n, send_k;

// Бит данных ставится после значения: Спектрум не прочтёт старое
static void data_out(uint8_t v) { reg_out = v, bit_data = 1; }
static uint8_t data_in(void) { bit_data = 0; return reg_in; }

// Как в ROM: старое значение в регистр вывода, новое из регистра ввода, бит данных в 0
static uint8_t data_swap(uint8_t old) { reg_out = old; return data_in(); }

static void send_start(int n) { send_n = (uint8_t)n, send_k = 1; data_out(send_buf[0]); }

static void send_ram(uint32_t v) {
    send_buf[0] = (uint8_t)v, send_buf[1] = (uint8_t)(v >> 8), send_buf[2] = (uint8_t)(v >> 16);
    send_start(3);
}

// Бит 7: 0 - значение изменилось с прошлого чтения, 1 - то же (#63, #64)
static uint8_t mark_changed(uint8_t v, uint8_t *old) {
    uint8_t r = v | (v == *old ? 0x80 : 0);
    *old = v;
    return r;
}

static void vol_apply(void) { gs_mod_set_vol(vol_master, vol_module); }

static void vars_reset(void) {
    stream = 0, ram_used = 0, mod_size = 0;
    mod_count = mod_cur = mod_playing = 0;
    vol_master = vol_module = VOL_MAX;
    send_n = send_k = 0;
    for (int i = 0; i < 4; i++) notes_old[i] = vols_old[i] = 0;
    vol_apply();
}

// #31, #65: запуск. В ответе - номер модуля, 0 - не запустился
static void play(uint8_t handle, uint8_t pos) {
    if (!handle) handle = mod_cur;
    if (!handle || handle > mod_count || !gs_mod_play(ram, (int)mod_size, pos)) {
        mod_cur = 0;
        data_out(0);
        return;
    }
    mod_cur = mod_playing = handle;
    vol_module = VOL_MAX;                           // ROM: при запуске MTVOL = #40
    vol_apply();
    gs_core_claim(&gs_dev_gs);
    data_out(handle);
}

static void cmd_run(uint8_t c) {
    switch (c) {
    case 0x00: case 0x08:                           // сброс флагов
        bit_data = 0;
        break;
    case 0x20: send_ram(ram_size); break;
    case 0x21: send_ram(ram_size - ram_used); break;
    case 0x23: {                                    // номер последней страницы: база 128К - 3
        uint32_t pages = ram_size / PAGE_SIZE;
        data_out(pages > 255 ? 255 : pages ? (uint8_t)(pages - 1) : 0);
        break;
    }
    case 0x2A: {
        uint8_t v = data_swap(vol_master);
        vol_master = v > VOL_MAX ? VOL_MAX : v;
        vol_apply();
        break;
    }
    case 0x30:                                      // загрузка: следующие байты данных - модуль
        gs_mod_stop();                              // данные старого сейчас затрём
        ram_used = 0, mod_size = 0;
        mod_count = mod_cur = 1, mod_playing = 0;
        reg_out = 1, bit_data = 0;
        stream = 1;
        break;
    case 0x31: play(data_in(), 0); break;
    case 0x32:
        reg_out = mod_playing, bit_data = 0;
        gs_mod_pause();
        break;
    case 0x33:
        reg_out = mod_playing, bit_data = 0;
        if (mod_playing && gs_mod_resume()) gs_core_claim(&gs_dev_gs);
        break;
    case 0x35: {
        uint8_t v = data_swap(vol_module);
        vol_module = v > VOL_MAX ? VOL_MAX : v;
        vol_apply();
        break;
    }
    case 0x36: data_out(0xFF); break;
    case 0x37:                                      // внутренние переменные - в исходное
        gs_mod_stop();
        vars_reset();
        break;
    case 0x60: data_out(gs_mod_song_pos()); break;
    case 0x61: data_out(gs_mod_pat_pos()); break;
    case 0x62: data_out((uint8_t)((gs_mod_song_pos() << 6) | (gs_mod_pat_pos() & 0x3F))); break;
    case 0x63:
        for (int i = 0; i < 4; i++) send_buf[i] = mark_changed(gs_mod_note(i), &notes_old[i]);
        send_start(4);
        break;
    case 0x64:
        for (int i = 0; i < 4; i++) send_buf[i] = mark_changed(gs_mod_vol(i), &vols_old[i]);
        send_start(4);
        break;
    case 0x65: play(mod_cur, data_in()); break;
    case 0x66: gs_mod_set_speed(data_in()); break;
    case 0x67: data_out(gs_mod_speed()); break;
    case 0x68: data_out(gs_mod_tempo()); break;
    case 0xD1: break;                               // поток и так открыт с #30
    case 0xD2:                                      // конец потока
        stream = 0;
        mod_size = ram_used;
        break;
    case 0xF3: case 0xF4:                           // перезапуск
        gs_mod_stop();
        vars_reset();
        bit_data = 0;
        break;
    default: break;                                 // эффекты (#38..) - следующий шаг
    }
}

static int  dev_enabled(void) { return gs_cfg.gs_on; }

static void dev_reset(void) {
    gs_mod_stop();
    reg_cmd = reg_in = reg_out = 0;
    bit_cmd = bit_data = 0;
    vars_reset();
}

static void dev_sound_stop(void) { gs_mod_stop(); }

// Шина: только быстро. Команды выполняет dev_work; поток - здесь, Спектрум ждать не должен
static void dev_port_write(uint8_t code, uint8_t v) {
    if (code == GS_PORT_BB) {
        reg_cmd = v, bit_cmd = 1;
    } else if (stream) {
        if (ram_used < ram_size) ram[ram_used++] = v;
    } else {
        reg_in = v, bit_data = 1;
    }
}

static uint8_t dev_port_read(uint8_t code) {
    if (code == GS_PORT_BB) return (uint8_t)(bit_data << 7 | 0x7E | bit_cmd);   // биты 1..6 на GS не определены
    bit_data = 0;
    return reg_out;
}

static void dev_work(void) {
    if (bit_cmd) {
        send_n = send_k = 0;                        // новая команда отменяет недосланный ответ, как HSEND в ROM
        cmd_run(reg_cmd);
        bit_cmd = 0;                                // после выполнения: по этому биту Спектрум читает ответ
    } else if (send_k < send_n && !bit_data) {
        data_out(send_buf[send_k++]);
    }
}

static void dev_audio(float *out, int n) { gs_mod_audio(out, n); }

const gs_dev_t gs_dev_gs = {
    "GS", dev_enabled, dev_reset, dev_sound_stop, dev_port_write, dev_port_read, dev_work, dev_audio
};

void gs_gs_set_ram(void *mem, uint32_t size) {
    ram = (uint8_t *)mem, ram_size = size;
    ram_used = 0;
}

int gs_gs_play_mod(const void *data, int size) {
    if (!gs_mod_load(data, size)) return 0;
    gs_core_claim(&gs_dev_gs);
    return 1;
}

void gs_gs_stop(void) { gs_mod_stop(); }
