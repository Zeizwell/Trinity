// Лицо GS: порты #BB / #B3 и команды. Протокол - FULGSCOM.TXT и main_ngs.a80 (ROM GS)
#include "gs_core.h"
#include "gs_cfg.h"
#include "gs_mod.h"
#include "gs_fx.h"
#include "gs_chan.h"
#include "gs_dev_gs.h"
#include "gs_ctl.h"
#include <string.h>

#define PAGE_SIZE 32768u
#define VOL_MAX   0x40

// Регистры. Биты - отдельными байтами: их меняют и шина, и ядро 0
static volatile uint8_t reg_cmd, reg_in, reg_out;
static volatile uint8_t bit_cmd, bit_data;

// Память модулей. Поток пишет шина, пока stream = 1
// Поток: шина кладёт в кольцо в ОЗУ, ядро 0 перекладывает в память. Почти полно - бит данных в 1, Z80 ждёт по протоколу
#define RING_SIZE 16384u
#define RING_HIGH (RING_SIZE - 64u)
static uint8_t ring[RING_SIZE];
static volatile uint32_t ring_wr, ring_rd;

static uint8_t *ram;
static uint32_t ram_cap;                           // сколько есть на самом деле

// Сколько видит GS: глобальная настройка, но не больше, чем есть. Читаем каждый раз - драйвер меняет на лету
static uint32_t ram_size(void) {
    uint32_t want = gs_cfg.gs_ram_kb * 1024u;
    return want < ram_cap ? want : ram_cap;
}
static volatile uint32_t ram_used;
static volatile uint8_t stream, stream_xor;        // xor: беззнаковый сэмпл эффекта -> со знаком

static uint32_t mod_base, mod_size, fx_base;
static int fx_cur, fx_loading;                      // текущий (#2E), грузится сейчас
static uint8_t fx_vol_a, fx_vol_b;                  // #2B, #3D
static uint8_t mod_count, mod_cur, mod_playing;     // один модуль: номер 1, 0 - нет
static uint8_t vol_master, vol_module;              // #2A, #35
static uint8_t notes_old[4], vols_old[4];           // для бита 7 в #63, #64

// Команда с несколькими байтами данных: следующие приходят после снятия бита команды
static uint8_t arg_cmd, arg_first, arg_buf[3], arg_need, arg_k;

// Ответ из нескольких байт: следующий - когда Спектрум прочёл предыдущий
static uint8_t send_buf[GS_CTL_OUT_MAX + 2], send_n, send_k;

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
    stream = 0, stream_xor = 0, ring_wr = ring_rd = 0, ram_used = 0, mod_base = 0, mod_size = 0;
    fx_cur = fx_loading = 0;
    fx_vol_a = fx_vol_b = VOL_MAX;
    arg_need = arg_k = 0;
    gs_fx_reset();
    mod_count = mod_cur = mod_playing = 0;
    vol_master = vol_module = VOL_MAX;
    send_n = send_k = 0;
    for (int i = 0; i < 4; i++) notes_old[i] = vols_old[i] = 0;
    vol_apply();
}

// #31, #65: запуск. В ответе - номер модуля, 0 - не запустился
static void play(uint8_t handle, uint8_t pos) {
    if (!handle) handle = mod_cur;
    if (!handle || handle > mod_count || !gs_mod_play(ram + mod_base, (int)mod_size, pos, GS_MOD_RAM)) {
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

static uint8_t vol_limit(uint8_t v) { return v > VOL_MAX ? VOL_MAX : v; }

static void fx_master_apply(void) { gs_fx_set_master((uint8_t)(fx_vol_a * fx_vol_b / VOL_MAX)); }

// #38, #3E: в регистр вывода номер, следующие байты данных - сэмпл
static void fx_load(uint8_t xor_v) {
    int h = gs_fx_new(ram + ram_used);
    reg_out = (uint8_t)h, bit_data = 0;
    if (!h) return;
    fx_cur = fx_loading = h;
    fx_base = ram_used;
    stream_xor = xor_v;
    stream = 1;
}

// #39
static void fx_play(uint8_t h) {
    if (!h) h = (uint8_t)fx_cur;
    fx_cur = h;
    if (!gs_fx_get(h)) { data_out(0xFF); return; }
    data_out(0);
    gs_fx_play(h);
    gs_core_claim(&gs_dev_gs);
}

// Параметр текущего эффекта: как в ROM, старое значение в регистр вывода
static void fx_param(uint8_t c) {
    gs_fx_t *f = gs_fx_get(fx_cur);
    uint8_t dummy = 0;
    uint8_t *p = !f ? &dummy : c == 0x40 ? &f->note : c == 0x41 ? &f->vol : c == 0x42 ? &f->finetune
               : c == 0x45 ? &f->prio : c == 0x46 ? &f->seek_first : &f->seek_last;
    *p = data_swap(*p);
}

// #80..#9B: номер канала в битах 0..1, бит 3 - нота, бит 4 - громкость
static void fx_direct(uint8_t c, uint8_t h, int note, int vol) {
    gs_fx_play_chan(h, c & 3, note, vol);
    gs_core_claim(&gs_dev_gs);
}

// ---- Код Z80, залитый в GS (#14 загрузить, #13 перейти) ----
// Z80 в карте нет: код не храним, считаем отпечаток - CRC32 первых байт. Известный код заменяет прошивка (заплатка,
// doc/zx-trinity.md), остальной пока ничего не делает (ловушка - todo п.71)
#define SIG_LEN    45u          // начало GS-части драйвера SD NeoGS: цикл команд #10 / #12 / #20
#define SIG_SD_NGS 0xDB92866Eu  // WC core32/drivers/SD_NGS.ASM (BUDDER/MGN 2015), залит в #4000

static uint32_t ld_left, ld_k, ld_crc;
static uint16_t ld_addr;

static uint32_t crc32_byte(uint32_t crc, uint8_t v) {
    crc ^= v;
    for (int i = 0; i < 8; i++) crc = crc >> 1 ^ (0xEDB88320u & (0u - (crc & 1)));
    return crc;
}

// #14: длина и адрес пришли, дальше байты кода по одному (бит данных)
static void load_start(uint16_t len, uint16_t addr) {
    ld_left = len, ld_addr = addr;
    ld_k = 0, ld_crc = 0xFFFFFFFFu;
}

static void load_byte(void) {
    uint8_t v = data_in();
    if (ld_k < SIG_LEN) ld_crc = crc32_byte(ld_crc, v);
    ld_k++, ld_left--;
}

static void gate_ngs(void);

// #13: переход на залитый код. Узнали - включаем заплатку
static void load_jump(uint16_t addr) {
    if (addr == ld_addr && ld_k >= SIG_LEN && ~ld_crc == SIG_SD_NGS) gate_ngs();
}

// ---- Шлюз секторов: протокол GS-части драйвера SD NeoGS ----
// #20 - init, ответ 0 / 1 (нет диска). #10 читать, #12 писать: перед командой в #B3 число секторов (0 = 256), потом
// номер 4 байта младшим вперёд и пустой байт; дальше 512 байт на сектор по биту данных. Конец - Спектрум шлёт команду
// 0 и ждёт снятия бита команды. #F3 / #F4 - выход, как перезапуск GS. Остальные команды в шлюзе ничего не делают.
// Пустой байт глотает шина: сразу за ним драйвер ждёт бит данных. Z80 NeoGS забирает его за ~1 мкс, ядро 0 может
// опоздать - Спектрум прочёл бы свой же пустой байт как первый байт сектора
static const gs_disk_t *disk;                       // SD карты (прошивка), 0 - нет
static const gs_disk_t *gate_disk;                  // диск шлюза сейчас: SD или тестовый

// Тестовый диск для проверки шины без SD: 16 секторов в ОЗУ, номер - по модулю
#define TEST_SECS 16
static uint8_t test_mem[TEST_SECS][512];
static int t_init(void) { return 1; }
static int t_read(uint32_t s, uint8_t *b) { memcpy(b, test_mem[s % TEST_SECS], 512); return 1; }
static int t_write(uint32_t s, const uint8_t *b) { memcpy(test_mem[s % TEST_SECS], b, 512); return 1; }
static const gs_disk_t test_disk = { t_init, t_read, t_write, 0 };
static uint8_t gate;                                // 1 - шлюз включён
static enum { SG_IDLE, SG_PAR, SG_DUMMY, SG_RD, SG_WR } sg_st;
static uint8_t sg_cmd, sg_par[5], sg_k;           // число секторов, номер 4 байта
static volatile uint8_t sg_swallow;                 // 1 - следующую запись #B3 (пустой байт) не принимать
static uint8_t sg_buf[512];
static uint32_t sg_sec, sg_left, sg_pos;

static void gate_on(const gs_disk_t *d) { gate_disk = d, gate = 1, sg_st = SG_IDLE, sg_swallow = 0; }
static void gate_ngs(void) { gate_on(disk); }        // драйвер NeoGS: всегда SD карты

static void gate_off(void) {
    gate = 0, sg_st = SG_IDLE;
    if (gate_disk && gate_disk->release) gate_disk->release();
}

// Номер и число секторов пришли: начинаем передачу
static void gate_par_done(void) {
    sg_sec = (uint32_t)sg_par[1] | (uint32_t)sg_par[2] << 8 | (uint32_t)sg_par[3] << 16 | (uint32_t)sg_par[4] << 24;
    sg_left = sg_par[0] ? sg_par[0] : 256u;
    if (sg_cmd == 0x10) sg_st = SG_RD, sg_pos = 512;    // сектор ещё не прочитан
    else sg_st = SG_WR, sg_pos = 0;
}

// Чтение: сектор с диска в буфер, байты - по одному, когда Спектрум забрал предыдущий
static void gate_read(void) {
    if (sg_pos == 512) {
        if (!sg_left) { sg_st = SG_IDLE; return; }
        if (!gate_disk || !gate_disk->read(sg_sec, sg_buf)) memset(sg_buf, 0xFF, sizeof sg_buf);
        sg_sec++, sg_left--, sg_pos = 0;
    }
    if (!bit_data) data_out(sg_buf[sg_pos++]);
}

// Запись: 512 байт в буфер, потом на диск
static void gate_write(void) {
    if (!bit_data) return;
    sg_buf[sg_pos++] = data_in();
    if (sg_pos < 512) return;
    if (gate_disk) gate_disk->write(sg_sec, sg_buf);
    sg_sec++, sg_left--, sg_pos = 0;
    if (!sg_left) sg_st = SG_IDLE;
}

static void gate_cmd(uint8_t c) {
    send_n = send_k = 0;
    switch (c) {
    case 0x20: data_out(gate_disk && gate_disk->init() ? 0 : 1); break;
    case 0x10: case 0x12:                           // команду приняли сразу, число секторов уже в #B3
        sg_cmd = c, sg_par[0] = data_in(), sg_k = 1;
        sg_st = SG_PAR;
        break;
    case 0xF3: case 0xF4:                           // выход: команду выполнит обычный GS
        gate_off();
        return;
    default: break;
    }
    bit_cmd = 0;
}

// Ядро 0, пока шлюз включён: передача сектора важнее новой команды (так и у NeoGS)
static void gate_work(void) {
    if (sg_st == SG_PAR) {
        if (!bit_data) return;
        if (sg_k == sizeof sg_par - 1) sg_swallow = 1;  // до снятия бита: после него Спектрум шлёт пустой байт
        sg_par[sg_k++] = data_in();
        if (sg_k == sizeof sg_par) sg_st = SG_DUMMY;
    } else if (sg_st == SG_DUMMY) {
        if (!sg_swallow) gate_par_done();           // пустой байт пришёл: только теперь свой бит данных - Спектрум
                                                    // ещё ждал снятия бита после последнего байта номера
    } else if (sg_st == SG_RD) gate_read();
    else if (sg_st == SG_WR) gate_write();
    else if (bit_cmd) gate_cmd(reg_cmd);
    else if (send_k < send_n && !bit_data) data_out(send_buf[send_k++]);     // ответ пакета GS_CTL_SD
}

// Пришли все байты команды
static void args_done(void) {
    uint8_t c = arg_cmd;
    if (c == 0x14) load_start((uint16_t)(arg_first | arg_buf[0] << 8), (uint16_t)(arg_buf[1] | arg_buf[2] << 8));
    else if (c == 0x13) load_jump((uint16_t)(arg_first | arg_buf[0] << 8));
    else if (c == 0x48 || c == 0x49) {
        gs_fx_t *f = gs_fx_get(fx_cur);
        uint32_t v = (uint32_t)arg_first | (uint32_t)arg_buf[0] << 8 | (uint32_t)arg_buf[1] << 16;
        if (f) { if (c == 0x48) f->loop_begin = v; else f->loop_end = v; }
    } else {
        int note = c & 8 ? arg_buf[0] : -1;
        int vol = c & 0x10 ? arg_buf[c & 8 ? 1 : 0] : -1;
        fx_direct(c, arg_first, note, vol);
    }
}

static void args_wait(uint8_t c, uint8_t first, int n) { arg_cmd = c, arg_first = first, arg_need = (uint8_t)n, arg_k = 0; }

// Канал драйвера (doc/trinity-api.md, раздел 7): свободные в GS команды за ключом. Без ключа они, как в ROM, ничего не делают;
// любая обычная команда GS закрывает канал и выполняется как всегда
static const char ctl_sign[] = GS_CTL_SIGN;
static uint8_t ctl_open, ctl_key_pos, ctl_in[GS_CTL_IN_MAX + 3];
static uint16_t ctl_n;

static void ctl_close(void) { ctl_open = 0, ctl_key_pos = 0, ctl_n = 0; }

static void ctl_key(void) {
    if (reg_in != (uint8_t)ctl_sign[ctl_key_pos]) { ctl_key_pos = 0; return; }   // данные не наши: остаются как есть
    bit_data = 0;
    if (ctl_sign[++ctl_key_pos]) return;
    ctl_key_pos = 0, ctl_open = 1, ctl_n = 0;
    data_out(GS_CTL_PROTO);
}

// Байт пакета [код][длина 2 байта][данные]. Последний - выполнить: Z80 ждёт бит команды, пока идёт запись во флеш, поэтому занято видно само
static void ctl_byte(void) {
    if (!ctl_open) return;
    uint8_t v = data_in();
    if (ctl_n < sizeof ctl_in) ctl_in[ctl_n] = v;
    ctl_n++;
    if (ctl_n < 3) return;
    int len = ctl_in[1] | ctl_in[2] << 8;
    if (ctl_n < 3 + len) return;
    int n;
    if (len > GS_CTL_IN_MAX) send_buf[2] = GS_CTL_ERR, n = 1;
    else n = gs_ctl_packet(ctl_in[0], ctl_in + 3, len, send_buf + 2);
    ctl_n = 0;
    send_buf[0] = (uint8_t)n, send_buf[1] = (uint8_t)(n >> 8);
    send_start(n + 2);
}

static void ctl_cmd(uint8_t c) {
    send_n = send_k = 0;
    if (c == GS_CTL_CMD_KEY) ctl_key();
    else if (c == GS_CTL_CMD_BYTE) ctl_byte();
    else ctl_close();
}

static void cmd_run(uint8_t c) {
    if ((c & 0xE4) == 0x80) {                       // #80..#83, #88..#8B, #90..#93, #98..#9B
        uint8_t h = data_in();
        int n = (c >> 3 & 1) + (c >> 4 & 1);
        if (!h) h = (uint8_t)fx_cur;
        fx_cur = h;
        if (!gs_fx_get(h)) { data_out(0xFF); return; }
        if (n) args_wait(c, h, n); else fx_direct(c, h, -1, -1);
        return;
    }
    switch (c) {
    case 0x13: args_wait(c, data_in(), 1); break;   // перейти: адрес 2 байта
    case 0x14: args_wait(c, data_in(), 3); break;   // загрузить: длина 2, адрес 2, потом байты
    case 0x1D:                                      // прочитать байт #20xx: WC узнаёт NeoGS по #8B в #2000
        data_out(data_in() == 0 ? 0x8B : 0xFF);
        break;
    case 0x00: case 0x08:                           // сброс флагов
        bit_data = 0;
        break;
    case 0x20: send_ram(ram_size()); break;
    case 0x21: send_ram(ram_size() > ram_used ? ram_size() - ram_used : 0); break;
    case 0x23: {                                    // номер последней страницы: база 128К - 3
        uint32_t pages = ram_size() / PAGE_SIZE;
        data_out(pages > 255 ? 255 : pages ? (uint8_t)(pages - 1) : 0);
        break;
    }
    case 0x2A: {
        uint8_t v = data_swap(vol_master);
        vol_master = vol_limit(v);
        vol_apply();
        break;
    }
    case 0x2B: {
        uint8_t v = data_swap(fx_vol_a);
        fx_vol_a = vol_limit(v);
        fx_master_apply();
        break;
    }
    case 0x2E: {
        uint8_t v = data_swap((uint8_t)fx_cur);
        if (gs_fx_get(v)) fx_cur = v;
        break;
    }
    case 0x30:                                      // загрузка: следующие байты данных - модуль
        gs_mod_stop();
        if (mod_count) vars_reset();                // как в ROM: второй модуль стирает всё, эффекты тоже
        mod_base = ram_used, mod_size = 0;
        mod_count = mod_cur = 1, mod_playing = 0;
        fx_loading = 0, stream_xor = 0;
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
        vol_module = vol_limit(v);
        vol_apply();
        break;
    }
    case 0x36: data_out(0xFF); break;
    case 0x37:                                      // внутренние переменные - в исходное
        gs_mod_stop();
        vars_reset();
        break;
    case 0x38: fx_load(0x80); break;
    case 0x39: fx_play(data_in()); break;
    case 0x3A: {                                    // как в ROM: бит 7 - канал 0, бит 6 - канал 1...
        uint8_t m = data_in(), chans = 0;
        for (int i = 0; i < GS_FX_CHANS; i++) if (m >> (7 - i) & 1) chans |= (uint8_t)(1 << i);
        gs_fx_stop(chans);
        break;
    }
    case 0x3D: {
        uint8_t v = data_swap(fx_vol_b);
        fx_vol_b = vol_limit(v);
        fx_master_apply();
        break;
    }
    case 0x3E: {                                    // 0 - беззнаковый, 1 - со знаком
        uint8_t v = data_in();
        if (v <= 1) fx_load(v ? 0 : 0x80); else reg_out = 0;
        break;
    }
    case 0x40: case 0x41: case 0x42: case 0x45: case 0x46: case 0x47: fx_param(c); break;
    case 0x48: case 0x49: args_wait(c, data_in(), 2); break;
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
        if (fx_loading) gs_fx_end(fx_loading, ram_used - fx_base), fx_loading = 0;
        else mod_size = ram_used - mod_base;
        stream_xor = 0;
        break;
    case 0xF3: case 0xF4:                           // перезапуск
        gs_mod_stop();
        vars_reset();
        bit_data = 0;
        break;
    default: break;
    }
}

static int  dev_enabled(void) { return gs_cfg.gs_on; }

static void dev_reset(void) {
    gs_chan_reset();
    gs_mod_stop();
    reg_cmd = reg_in = reg_out = 0;
    ctl_close();
    bit_cmd = bit_data = 0;
    ld_left = 0, gate = 0, sg_st = SG_IDLE, sg_swallow = 0;
    vars_reset();
}

static void dev_sound_stop(void) {
    gs_mod_stop();
    gs_fx_stop(0x0F);
}

// Шина: только быстро. Команды выполняет dev_work; поток - здесь, Спектрум ждать не должен
static void dev_port_write(uint8_t code, uint8_t v) {
    if (code == GS_PORT_BB) {
        reg_cmd = v, bit_cmd = 1;
    } else if (sg_swallow) {
        sg_swallow = 0;                             // пустой байт шлюза секторов
    } else if (stream) {
        uint32_t fill = ring_wr - ring_rd;
        if (fill < RING_SIZE) ring[ring_wr & (RING_SIZE - 1)] = v ^ stream_xor, ring_wr++;
        if (fill >= RING_HIGH) bit_data = 1;
    } else {
        reg_in = v, bit_data = 1;
    }
}

static uint8_t dev_port_peek(uint8_t code) {
    if (code == GS_PORT_BB) return (uint8_t)(bit_data << 7 | 0x7E | bit_cmd);   // биты 1..6 на GS не определены
    return reg_out;
}

static uint8_t dev_port_read(uint8_t code) {
    uint8_t v = dev_port_peek(code);
    if (code != GS_PORT_BB) bit_data = 0;
    return v;
}

// Поток из кольца в память
static void ring_drain(void) {
    while (ring_rd != ring_wr) {
        uint8_t v = ring[ring_rd & (RING_SIZE - 1)];
        if (ram_used < ram_size()) ram[ram_used] = v, ram_used++;
        ring_rd++;
    }
    if (stream) bit_data = 0;                       // Z80 ждал места
}

static void dev_work(void) {
    if (gate) { gate_work(); return; }
    if (ring_rd != ring_wr) ring_drain();
    if (bit_cmd) {
        ring_drain();                               // до команды: #D2 должен видеть все байты
        uint8_t c = reg_cmd;
        if (c == GS_CTL_CMD_KEY || c == GS_CTL_CMD_BYTE || c == GS_CTL_CMD_CLOSE) ctl_cmd(c);     // без замка: сам берёт его, где надо; запись во флеш - вне замка
        else {
            ctl_close();
            gs_core_lock();
            send_n = send_k = 0;                    // новая команда отменяет недосланный ответ, как HSEND в ROM
            arg_need = arg_k = 0, ld_left = 0;      // и загрузку кода (#14), как COMINT1 в ROM
            cmd_run(c);
            gs_core_unlock();
        }
        bit_cmd = 0;                                // после выполнения: по этому биту Спектрум читает ответ
    } else if (arg_k < arg_need) {
        if (bit_data) {
            arg_buf[arg_k++] = data_in();
            if (arg_k == arg_need) {
                arg_need = arg_k = 0;
                gs_core_lock();
                args_done();
                gs_core_unlock();
            }
        }
    } else if (ld_left) {
        if (bit_data) load_byte();
    } else if (send_k < send_n && !bit_data) {
        data_out(send_buf[send_k++]);
    }
}

static void dev_audio(float *out, int n) { gs_mod_audio(out, n); }

const gs_dev_t gs_dev_gs = {
    "GS", dev_enabled, dev_reset, dev_sound_stop, dev_port_write, dev_port_read, dev_port_peek, dev_work, dev_audio
};

void gs_gs_set_disk(const gs_disk_t *d) { disk = d; }

// Пакет GS_CTL_SD (ядро 0, внутри dev_work): ответ уйдёт уже из шлюза
int gs_gs_sd_gate(int test) {
    const gs_disk_t *d = test ? &test_disk : disk;
    if (!d) return 0;
    gate_on(d);
    return 1;
}

void gs_gs_set_ram(void *mem, uint32_t size) {
    ram = (uint8_t *)mem, ram_cap = size;
    ram_used = 0;
}

int gs_gs_play_mod(const void *data, int size, gs_mod_mem_t mem) {
    if (!gs_mod_play(data, size, 0, mem)) return 0;
    gs_core_claim(&gs_dev_gs);
    return 1;
}

void gs_gs_stop(void) { gs_mod_stop(); }
