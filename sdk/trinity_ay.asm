; =========================================================================
; ZX Trinity AY FX - SDK for Z80 programs / SDK для программ Z80
; -------------------------------------------------------------------------
; EN: Timer effects card on the AY ports (SID, Sync, FM, Env FM). The card sits in the AY bus and is talked to
;     through two free AY register numbers. Include this file where the code should go: constants take no
;     memory, routines are a few bytes each. Packet codes live here only - change them here, everyone follows.
;     Protocol: FT2 doc/ay-ports-map.md 5a, FX row: FT2/doc/fx-row.md.
; RU: Карта таймерных эффектов на портах AY (SID, Sync, FM, Env FM). Стоит в шине AY, говорим с ней через два
;     свободных номера регистра AY. Подключать файл там, где должен лечь код: константы памяти не занимают,
;     процедуры - по несколько байт. Коды пакетов - только здесь: правим здесь, все берут отсюда.
;     Протокол: FT2 doc/ay-ports-map.md п.5а, строка FX - FT2/doc/fx-row.md.
;
; EN: Usage: select the chip (#FF / #FE to #FFFD for TurboSound), C = #FD, then call the routines.
;     Every routine keeps C = #FD and the selected chip; B and A are spoiled.
; RU: Порядок: выбрать чип (#FF / #FE в #FFFD для TurboSound), C = #FD, дальше звать процедуры.
;     Все процедуры оставляют C = #FD и выбранный чип; портят B и A.
; =========================================================================

; --- Ports / Порты
AYFX_PORT_REG   equ #FFFD           ; EN: register number    RU: номер регистра
AYFX_PORT_DAT   equ #BFFD           ; EN: register data      RU: данные регистра

; --- Card registers (number written to #FFFD) / Регистры карты (номер в #FFFD), у каждого чипа свои
AYFX_CTL        equ #C0             ; EN: control: key "TRIN" opens, 0 closes; read -> status byte (trinity-api.md 3)
                                    ; RU: управление: ключ "TRIN" - открыть, 0 - закрыть; чтение - байт статуса
AYFX_STREAM     equ #C1             ; EN: packet stream [code][data...]   RU: поток пакетов [код][данные...]

; --- Packets in #C1 / Пакеты в #C1 (EN: length includes the code; RU: длина - вместе с кодом)
AYFX_P_ROW      equ 1               ; EN: ROW: channel 0..2, FX row 14 bytes, sweep shape, phase, flags
                                    ; RU: ROW: канал 0..2, строка FX 14 байт, форма, фаза, флаги свипа
AYFX_ROW_LEN    equ 19
AYFX_P_TABLE    equ 2               ; EN: TABLE: number 0..255, 16 values   RU: TABLE: номер 0..255, 16 значений
AYFX_TABLE_LEN  equ 18
AYFX_P_RESET    equ 3               ; EN: RESET: channel mask - new note     RU: RESET: маска каналов - новая нота
AYFX_RESET_LEN  equ 2
AYFX_P_INFO     equ 4               ; EN: INFO: answer in #C1 - "TRIN", version (Ayfx_InfoSend)   RU: INFO: ответ в #C1 - "TRIN", версия
AYFX_INFO_LEN   equ 1
AYFX_INFO_N     equ 5               ; EN: INFO answer bytes: "TRIN", protocol version   RU: байт ответа INFO: "TRIN", версия
AYFX_P_MODE     equ #C0             ; EN: service MODE: [code][length 2][state], answer [length 2][0 done / FF not allowed]
                                    ; RU: служебный MODE: [код][длина 2][состояние], ответ [длина 2][0 сделано / FF нельзя]
AYFX_MODE_AY    equ 0               ; EN: state AY - default (Spectrum commands + FX)   RU: состояние AY - по умолчанию
AYFX_MARK       equ #80             ; EN: into #C0 after a service packet: status "busy" at once, until the answer is ready
                                    ; RU: в #C0 после служебного пакета: статус «занята» сразу, пока ответ не готов
AYFX_ST_BUSY    equ 7               ; EN: status bit: service packet is being parsed   RU: бит статуса: разбирается пакет
AYFX_ST_REPLY   equ 6               ; EN: status bit: answer ready in #C1              RU: бит статуса: ответ готов в #C1
AYFX_SVC_DONE   equ 0               ; EN: Ayfx_SvcState: done, no answer               RU: Ayfx_SvcState: разобрал, ответа нет
AYFX_SVC_BUSY   equ 1               ; EN: still parsing                                RU: ещё разбирает
AYFX_SVC_REPLY  equ 2               ; EN: answer ready                                 RU: ответ готов

; --- Service packets (Ayfx_SvcSend / Ayfx_SvcState / Ayfx_SvcReply; trinity-api.md 2) / Служебные пакеты: [code][length 2][data], LSB first / мл. байт вперёд
AYFX_P_SLOT_NEW   equ #80           ; EN: type 4, size 4, flags -> slot (0 - no room)   RU: тип 4, размер 4, флаги -> слот (0 - нет места)
AYFX_P_SLOT_WRITE equ #81           ; EN: slot, offset 4, bytes (up to 512) - no answer   RU: слот, смещение 4, байты (до 512) - без ответа
AYFX_P_SLOT_READ  equ #82           ; EN: slot, offset 4, count 2 -> bytes               RU: слот, смещение 4, сколько 2 -> байты
AYFX_P_SLOT_FREE  equ #84           ; EN: slot - no answer                                RU: слот - без ответа
AYFX_P_SVC_CALL   equ #91           ; EN: service, command, in slot, out slot, params (up to 8) -> 0 ok, 1 busy, 2 no service,
                                    ;     3 wrong input, 4 no such command
                                    ; RU: сервис, команда, слот входа, слот выхода, параметры (до 8) -> 0 пошло, 1 занято,
                                    ;     2 нет сервиса, 3 вход не тот, 4 нет такой команды
AYFX_P_JOB_STATUS equ #A0           ; EN: -> state (0 none, 1 runs, 2 done, 3 error), progress 0..100, log slot, log bytes 4
                                    ; RU: -> состояние (0 нет, 1 идёт, 2 готово, 3 ошибка), прогресс 0..100, слот лога, байт лога 4
AYFX_P_JOB_LOG    equ #A1           ; EN: -> log text (up to 512)   RU: -> текст лога (до 512)
SVC_JOB_NONE    equ 0               ; EN: job states   RU: состояния задания
SVC_JOB_RUN     equ 1
SVC_JOB_DONE    equ 2
SVC_JOB_ERR     equ 3

; --- Service 1: player (Atari SNDH) / Сервис 1: плеер. PLAY: in - slot 'SNDH', param 0 - song (0 = first), out - slot
;     'STAT' or 0 (the card makes one). Then the same service: STOP, PAUSE (param 0: 1 pause, 0 go on), SEEK (params 0..3 -
;     frame of 20 ms). The card leaves FX / AY by itself; MODE AY, reset or STOP - the chip is Spectrum's again.
;     RU: PLAY: вход - слот 'SNDH', параметр 0 - песня (0 = первая), выход - слот 'STAT' или 0 (карта сделает сама). Дальше тому
;     же сервису: STOP, PAUSE (параметр 0: 1 пауза, 0 дальше), SEEK (параметры 0..3 - кадр 20 мс). Из FX / AY карта уходит
;     сама; MODE AY, сброс или STOP - чип снова у Спектрума.
ATARI_SVC       equ 1
ATARI_PLAY      equ 1
ATARI_STOP      equ 2
ATARI_PAUSE     equ 3
ATARI_SEEK      equ 4
ATARI_ST_FRAME  equ 0               ; EN: STAT: 4 - frames played   RU: STAT: 4 - кадров сыграно
ATARI_ST_FRAMES equ 4               ; EN: 4 - song length in frames (0 - unknown)   RU: 4 - длина песни в кадрах (0 - неизвестна)
ATARI_ST_SONG   equ 8               ; EN: 1 - song, 1 - songs in the file   RU: 1 - песня, 1 - песен в файле
ATARI_ST_FLAGS  equ 10              ; EN: bit 0 - pause, bit 1 - seeking    RU: бит 0 - пауза, бит 1 - перемотка
ATARI_ST_LOAD   equ 12              ; EN: 2 - us to compute the last frame (of 20000), 2 - peak   RU: 2 - мкс счёта кадра (из 20000), 2 - пик
ATARI_ST_LATE   equ 16              ; EN: 4 - frames late (the chip waited)  RU: 4 - кадр опоздал (чип ждал)
ATARI_ST_LOST   equ 20              ; EN: 4 - writes that did not fit        RU: 4 - записей не влезло
ATARI_ST_SIZE   equ 32

;-------- EN: Open the card: key "TRIN" into #C0 (stream parser restarts)
;         RU: Открыть карту: ключ "TRIN" в #C0 (разбор потока - с начала)
Ayfx_Key
        ld b,high AYFX_PORT_REG
        ld a,AYFX_CTL
        out (c),a
        ld b,high AYFX_PORT_DAT
        ld a,'T'
        out (c),a
        ld a,'R'
        out (c),a
        ld a,'I'
        out (c),a
        ld a,'N'
        out (c),a
        ret

;-------- EN: Close the card: 0 into #C0     RU: Закрыть карту: 0 в #C0
Ayfx_Close
        ld b,high AYFX_PORT_REG
        ld a,AYFX_CTL
        out (c),a
        ld b,high AYFX_PORT_DAT
        xor a
        out (c),a
        ret

;-------- EN: Close and mark busy: 0, then AYFX_MARK into #C0. The card shows "busy" from this moment until the
;             answer lies in #C1 - a status read never comes too early
;         RU: Закрыть и пометить «занята»: 0, потом AYFX_MARK в #C0. Карта показывает «занята» с этого момента, пока
;             ответ не ляжет в #C1 - чтение статуса не бывает слишком ранним
Ayfx_CloseMark
        call Ayfx_Close
        ld a,AYFX_MARK
        out (c),a
        ret

;-------- EN: Select the stream #C1: then write packet bytes to #BFFD (B = #BF)
;         RU: Выбрать поток #C1: дальше байты пакетов в #BFFD (B = #BF)
Ayfx_Stream
        ld b,high AYFX_PORT_REG
        ld a,AYFX_STREAM
        out (c),a
        ld b,high AYFX_PORT_DAT
        ret

;-------- EN: Ask INFO (is the card here?): key, stream, INFO, close and mark. Does not wait: the program polls
;             Ayfx_SvcState in its own loop (#FF - no card; the loop gives up after its own limit), at AYFX_SVC_REPLY -
;             Ayfx_Detect or Ayfx_Info
;         RU: Спросить INFO (есть ли карта): ключ, поток, INFO, закрыть и пометить. Не ждёт: программа опрашивает
;             Ayfx_SvcState в своём цикле (#FF - карты нет; цикл сдаётся по своему пределу), на AYFX_SVC_REPLY -
;             Ayfx_Detect или Ayfx_Info
Ayfx_InfoSend
        call Ayfx_Key
        call Ayfx_Stream
        ld a,AYFX_P_INFO
        out (c),a
        jp Ayfx_CloseMark

;-------- EN: Read the INFO answer (after Ayfx_InfoSend, Ayfx_SvcState = AYFX_SVC_REPLY): "TRIN"? -> A = 1, NZ - card;
;             A = 0, Z - not ours. Reads the answer whole. Keeps HL, DE
;         RU: Прочитать ответ INFO (после Ayfx_InfoSend, Ayfx_SvcState = AYFX_SVC_REPLY): "TRIN"? -> A = 1, NZ - карта;
;             A = 0, Z - не наша. Читает ответ целиком. HL, DE сохраняет
Ayfx_Detect
        push hl
        push de
        ld b,high AYFX_PORT_REG     ; EN: #C1 afresh, read #FFFD   RU: #C1 заново, чтение #FFFD
        ld a,AYFX_STREAM
        out (c),a
        ld hl,Ayfx_Sign
        ld de,4*256+1               ; EN: D - letters, E - result   RU: D - букв, E - итог
.r      in a,(c)
        cp (hl)
        jr z,$+4
        ld e,0                      ; EN: not the letter - not ours   RU: не та буква - не наша
        inc hl
        dec d
        jr nz,.r
        in a,(c)                    ; EN: 5th byte - version (read the answer out)   RU: 5-й байт - версия (дочитать)
        ld a,e
        or a
        pop de
        pop hl
        ret
Ayfx_Sign       db "TRIN"

;-------- EN: Read the INFO answer into HL (after Ayfx_InfoSend, Ayfx_SvcState = AYFX_SVC_REPLY): AYFX_INFO_N bytes -
;             "TRIN", protocol version. HL - after the last byte. Spoils A, B, D
;         RU: Прочитать ответ INFO в HL (после Ayfx_InfoSend, Ayfx_SvcState = AYFX_SVC_REPLY): AYFX_INFO_N байт -
;             "TRIN", версия протокола. HL - за последним байтом. Портит A, B, D
Ayfx_Info
        ld b,high AYFX_PORT_REG     ; EN: #C1 afresh, read #FFFD   RU: #C1 заново, чтение #FFFD
        ld a,AYFX_STREAM
        out (c),a
        ld d,AYFX_INFO_N
.r      in a,(c)
        ld (hl),a
        inc hl
        dec d
        jr nz,.r
        ret

;-------- EN: Card to default: MODE AY - stops the self-player (Atari etc.), the chip is Spectrum's again. Call it at
;             player init. Only sends the packet (close and mark): the result - Ayfx_SvcState / Ayfx_SvcReply
;             (1 byte: 0 done)
;         RU: Карту в состояние по умолчанию: MODE AY - стоп самоплеера (Atari и т.п.), чип снова у Спектрума. Звать
;             при инициализации плеера. Только отправляет пакет (закрыть и пометить): итог - Ayfx_SvcState /
;             Ayfx_SvcReply (1 байт: 0 сделано)
Ayfx_ModeAy
        call Ayfx_Key
        call Ayfx_Stream
        ld a,AYFX_P_MODE            ; EN: packet: code, length 1, state   RU: пакет: код, длина 1, состояние
        out (c),a
        ld a,1
        out (c),a
        xor a
        out (c),a
        ld a,AYFX_MODE_AY
        out (c),a
        jp Ayfx_CloseMark

;-------- EN: Send a service packet from HL (DE bytes: code, length 2, data): key, stream, bytes, close and mark.
;             Does not wait: the program polls Ayfx_SvcState in its own loop, then Ayfx_SvcReply. Next packet - only
;             after the card is not busy (a packet sent while busy is lost). Between the packet and the answer read -
;             no other writes to #C0 / #C1 (FX player: hold it). HL - after the packet
;         RU: Отправить служебный пакет с HL (DE байт: код, длина 2, данные): ключ, поток, байты, закрыть и пометить.
;             Не ждёт: программа опрашивает Ayfx_SvcState в своём цикле, потом Ayfx_SvcReply. Следующий пакет - только
;             когда карта не занята (пакет в занятую карту теряется). Между пакетом и чтением ответа - никаких других
;             записей в #C0 / #C1 (плеер FX - придержать). HL - за пакетом
Ayfx_SvcSend
        call Ayfx_Key
        call Ayfx_Stream
.b      ld a,(hl)
        out (c),a
        inc hl
        dec de
        ld a,d
        or e
        jr nz,.b
        jp Ayfx_CloseMark

;-------- EN: One status read (#C0, no key needed). -> A: AYFX_SVC_DONE (0) - done, no answer; AYFX_SVC_BUSY (1) -
;             still parsing; AYFX_SVC_REPLY (2) - answer ready (Ayfx_SvcReply); #FF - no card (a live card never gives
;             #FF: chip field 0..3, FX bit only with chip 0). Z flag: A = 0
;         RU: Одно чтение статуса (#C0, ключ не нужен). -> A: AYFX_SVC_DONE (0) - разобрал, ответа нет; AYFX_SVC_BUSY (1)
;             - ещё разбирает; AYFX_SVC_REPLY (2) - ответ готов (Ayfx_SvcReply); #FF - карты нет (живая карта #FF не
;             даёт: поле чипа 0..3, бит FX - только при чипе 0). Флаг Z: A = 0
Ayfx_SvcState
        ld b,high AYFX_PORT_REG
        ld a,AYFX_CTL
        out (c),a
        in a,(c)
        cp #FF
        ret z
        bit AYFX_ST_BUSY,a
        jr nz,.busy
        and 1 << AYFX_ST_REPLY      ; EN: 0 - done, else answer   RU: 0 - разобрал, иначе ответ
        ret z
        ld a,AYFX_SVC_REPLY
        ret
.busy   ld a,AYFX_SVC_BUSY
        or a
        ret

;-------- EN: Read the answer (after Ayfx_SvcState = AYFX_SVC_REPLY) to HL: #C1 afresh, length 2, bytes - whole (no
;             key needed). -> HL = answer, DE = its bytes, A = its first byte (result)
;         RU: Прочитать ответ (после Ayfx_SvcState = AYFX_SVC_REPLY) в HL: #C1 заново, длина 2, байты - целиком (ключ
;             не нужен). -> HL = ответ, DE = его байт, A = первый байт (итог)
Ayfx_SvcReply
        ld b,high AYFX_PORT_REG     ; EN: read #FFFD = selected #C1   RU: чтение #FFFD - выбранный #C1
        ld a,AYFX_STREAM
        out (c),a
        in e,(c)
        in d,(c)
        push hl
        push de
.d      ld a,d
        or e
        jr z,.e
        in a,(c)
        ld (hl),a
        inc hl
        dec de
        jr .d
.e      pop de
        pop hl
        ld a,(hl)
        ret

;-------- EN: Send D bytes of packets from HL (D = 1..255): key, stream, bytes, close. HL - after the last byte
;         RU: Отправить D байт пакетов с HL (D = 1..255): ключ, поток, байты, закрыть. HL - за последним байтом
Ayfx_Send
        call Ayfx_Key
        call Ayfx_Stream
.b      ld a,(hl)                   ; EN: OUT (C),A with constant BC = #BFFD   RU: OUT (C),A с постоянным BC = #BFFD
        out (c),a
        inc hl
        dec d
        jr nz,.b
        jp Ayfx_Close
