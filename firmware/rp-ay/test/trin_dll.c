// Карта Trinity для эмулятора (наш Unreal, FT2/unreal): та же прошивка (main.c, card.c, svc.c, atari.c, ...) на
// модели железа (mock/sim.c), что и test.bat, - собрана в trinity_card.dll. Эмулятор зовёт:
//   trin_init  - старт прошивки (белый -> тест чипа -> работа), консоль карты - в файл log;
//   trin_write - Спектрум пишет #FFFD (a0 = 0) / #BFFD (a0 = 1): все записи, карта сама следит за выбором чипа;
//   trin_read  - Спектрум читает #FFFD (только когда выбран чип карты);
//   trin_run   - прошло us времени Спектрума: оба ядра карты на это время (служебное раз в мс, «Звук» и чип);
//   trin_regs  - регистры чипа карты (модель YM): их эмулятор и играет вместо своего AY чипа 0.
// Сборка - emu.bat. Своей логики здесь нет: только склейка, как run_ms в host_test.c
#define main fw_main
#include "../main.c"
#undef main
#include <stdio.h>

void hwm_write(bool a0, uint8_t v);
uint8_t hwm_read(void);

#define DLL __declspec(dllexport)

static int trin_on;

DLL int trin_init(const char *log) {
    if (log && *log) {                                              // консоль карты (printf прошивки) - в файл
        freopen(log, "w", stdout);
        setvbuf(stdout, NULL, _IONBF, 0);
    }
    sim_run_main(fw_main, 8000);                                    // старт прошивки, как host_test.c п.1
    trin_on = boot_result == 0;
    printf("trin_init: boot_result %d\n", boot_result);
    return trin_on;
}

DLL void trin_write(int a0, uint8_t v) { if (trin_on) hwm_write(a0 != 0, v); }

DLL uint8_t trin_read(void) { return trin_on ? hwm_read() : 0xFF; }

// Записи в чип карты за trin_run: время от начала прогона (нс), регистр, значение - эмулятор играет их в своём AY
// в те же моменты (иначе таймерные эффекты и цифра SNDH - шум)
typedef struct { uint32_t ns; uint8_t reg, val; } trin_wr_t;
#define WR_MAX 8192
static trin_wr_t wr[WR_MAX];
static int wr_n;
static uint8_t shadow[14];

// ---- Сторож зависаний: trin_run идёт дольше 3 с реального времени - отдельный поток останавливает поток
// эмулятора и пишет в лог его стек (имена функций из PDB). Только для отладки модели: на железе - watchdog
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
static HANDLE run_thread;                                   // поток эмулятора (в trin_run)
static volatile LONG run_seq, run_in;                       // номер прогона, 1 - внутри trin_run
static DWORD WINAPI hang_watch(LPVOID arg) {
    (void)arg;
    LONG seen = -1; int same = 0;
    for (;;) {
        Sleep(500);
        LONG s = run_seq;
        if (!run_in || s != seen) { seen = s, same = 0; continue; }
        if (++same != 6) continue;                          // 3 с в одном прогоне
        SuspendThread(run_thread);
        CONTEXT cx; memset(&cx, 0, sizeof cx); cx.ContextFlags = CONTEXT_FULL;
        GetThreadContext(run_thread, &cx);
        HANDLE pr = GetCurrentProcess();
        SymInitialize(pr, NULL, TRUE);
        STACKFRAME64 f; memset(&f, 0, sizeof f);
        f.AddrPC.Offset = cx.Rip, f.AddrFrame.Offset = cx.Rbp, f.AddrStack.Offset = cx.Rsp;
        f.AddrPC.Mode = f.AddrFrame.Mode = f.AddrStack.Mode = AddrModeFlat;
        printf("HANG: trin_run > 3 s, model time %.3f s, stack:\n", (double)sim_now_ns() / 1e9);
        for (int i = 0; i < 24 && StackWalk64(IMAGE_FILE_MACHINE_AMD64, pr, run_thread, &f, &cx, NULL,
                                               SymFunctionTableAccess64, SymGetModuleBase64, NULL); i++) {
            char b[sizeof(SYMBOL_INFO) + 256]; SYMBOL_INFO *si = (SYMBOL_INFO *)b;
            memset(b, 0, sizeof b); si->SizeOfStruct = sizeof(SYMBOL_INFO); si->MaxNameLen = 255;
            DWORD64 d = 0; IMAGEHLP_LINE64 ln; DWORD ld = 0; memset(&ln, 0, sizeof ln); ln.SizeOfStruct = sizeof ln;
            bool hs = SymFromAddr(pr, f.AddrPC.Offset, &d, si) != 0;
            bool hl = SymGetLineFromAddr64(pr, f.AddrPC.Offset, &ld, &ln) != 0;
            printf("  %s  %s:%lu\n", hs ? si->Name : "?", hl ? ln.FileName : "?", hl ? (unsigned long)ln.LineNumber : 0ul);
        }
        ResumeThread(run_thread);
    }
}

// Оба ядра на us модельного времени (как run_ms в host_test.c); смены регистров чипа - в wr
DLL void trin_run(uint32_t us) {
    static uint64_t next;
    wr_n = 0;
    if (!trin_on) return;
    if (!run_thread) {
        DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &run_thread, 0, FALSE,
                        DUPLICATE_SAME_ACCESS);
        CreateThread(NULL, 0, hang_watch, NULL, 0, NULL);
    }
    InterlockedIncrement(&run_seq);
    run_in = 1;
    uint64_t t0 = sim_now_ns(), end = t0 + (uint64_t)us * 1000;
    while (sim_now_ns() < end) {
        if (sim_now_ns() >= next) card_svc_poll(false), next = sim_now_ns() + 1000000;
        card_poll(); card_sound(); chip_poll();
        static uint32_t seen;                                       // регистры смотрим, только когда в чип писали
        if (sim_ym_writes == seen) continue;
        seen = sim_ym_writes;
        for (int r = 0; r < 14; r++) {
            if (sim_ym_regs[r] == shadow[r]) continue;
            shadow[r] = sim_ym_regs[r];
            if (wr_n < WR_MAX) wr[wr_n].ns = (uint32_t)(sim_now_ns() - t0), wr[wr_n].reg = (uint8_t)r, wr[wr_n].val = shadow[r], wr_n++;
        }
    }
    run_in = 0;
}

// Записи последнего trin_run -> n
DLL const trin_wr_t *trin_writes(int *n) { *n = wr_n; return wr; }

DLL const uint8_t *trin_regs(void) { return sim_ym_regs; }

// Такт φM чипа карты, Гц (0 - Спектрума: карта его не задаёт)
DLL uint32_t trin_phim(void) { return sim_phim_hz; }
