// Карта со стороны Спектрума (doc/zx-trinity.md 14б). Два уровня, связь - очередь событий (один пишет,
// один читает, без блокировок):
//   «Шина» (прерывание PIO, ядро 1): записи слота -> тень, выбор чипа TurboSound, протокол AY FX (ayfx_bus_t),
//            ответ на чтение - сразу; в очередь - события: запись в регистр AY, пакет. Закрытие потока эффекты
//            не выключает (ay-ports-map.md п.4) - только ROW нулей, таймаут, сброс.
//   «Звук»: фон ядра 1 (card_poll) - события -> движок эффектов (ayfx_t): планы (всё тяжёлое), записи Спектрума;
//            прерывание будильника (card_sound) - шаги эффектов точно в срок по готовым планам, записи эффектов
//            в чип - вне очереди.
// Чип пишет только chip.c.
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define CARD_CHIPS 2                        // чипов TurboSound максимум (cfg.chips - сколько включено)

// ---- «Шина» (из прерывания)
void    card_slot_write(bool a0, uint8_t v);// запись Спектрума: A0 = 0 - адрес (#FFFD), 1 - данные (#BFFD)
uint8_t card_answer(void);                  // что отдать на чтение данных сейчас
void    card_read_done(void);               // данные прочитали (метка PIO) - ответ-поток сдвинуть
uint8_t card_sel(void);                     // выбранный регистр текущего чипа (лог)

// ---- «Служебное» (ядро 0, doc/trinity-api.md): служебные пакеты -> ответ, задания, байт статуса
void    card_svc_poll(bool error);              // звать часто (~1 мс); error - ошибка карты (тест чипа при старте)

// ---- «Звук»
#define CARD_SOUND_MAX_US 1000              // будильник - не реже
void    card_poll(void);                    // фон ядра 1: события шины -> планы эффектов и чип; кадр свипа; таймаут
uint32_t card_sound(void);                  // прерывание будильника: шаги по готовым планам; -> мкс до следующего вызова
void    sound_kick(void);                   // (main.c) будильник - сейчас (планы поменялись)
void    phim_set(uint32_t hz);              // (main.c) такт чипов φM, Гц; 0 - как для Спектрума (cfg.phim_hz)
void    card_atari(bool on);                // (ядро 0) просьба ядру 1: on - режим ATARI (из AY / FX - через MODE AY), off - был
                                            //   ATARI - карта по умолчанию (MODE AY)
bool    card_atari_pending(void);           // (ядро 0) просьбы card_atari ещё не выполнены ядром 1
void    card_reset(void);                   // сброс (/IC, включение): всё; звать при выключенных прерываниях
void    card_resend(void);                  // тень Спектрума (SSG 0..13) - снова в чип (после теста)
void    card_test_bytes(const uint8_t *b, int n);   // итог теста чипа - в тень #E0.. (номера, что никто не пишет):
                                            //   читать #FFFD; сброс не стирает. Раскладка - main.c, boot_check
extern volatile uint32_t card_ev_lost;      // событий потеряно (очередь была полна)
extern volatile uint32_t card_late_max, card_late_sum, card_late_n;   // опоздание шагов эффектов, мкс (консоль)
extern volatile uint32_t card_pk[5];        // пакетов разобрано по кодам (1 ROW, 2 TABLE, 3 RESET), консоль
#define CARD_SVC_LOG 16
extern volatile uint32_t card_svc_log[CARD_SVC_LOG], card_svc_n;   // журнал служебных пакетов (card.c), консоль
extern volatile uint16_t card_svc_len[CARD_SVC_LOG], card_svc_rep[CARD_SVC_LOG];   // длина ответа, сколько раз подряд
