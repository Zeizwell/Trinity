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
AYFX_P_INFO     equ 4               ; EN: INFO: then read #C1 - "TRIN", version   RU: INFO: дальше читать #C1 - "TRIN", версия
AYFX_INFO_LEN   equ 1
AYFX_INFO_N     equ 5               ; EN: INFO answer bytes: "TRIN", protocol version   RU: байт ответа INFO: "TRIN", версия
AYFX_P_MODE     equ #C0             ; EN: service MODE: [code][length 2][state], answer [length 2][0 done / FF not allowed]
                                    ; RU: служебный MODE: [код][длина 2][состояние], ответ [длина 2][0 сделано / FF нельзя]
AYFX_MODE_AY    equ 0               ; EN: state AY - default (Spectrum commands + FX)   RU: состояние AY - по умолчанию
AYFX_ST_BUSY    equ 7               ; EN: status bit: service packet is being parsed   RU: бит статуса: разбирается пакет
AYFX_ST_REPLY   equ 6               ; EN: status bit: answer ready in #C1              RU: бит статуса: ответ готов в #C1

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

;-------- EN: Select the stream #C1: then write packet bytes to #BFFD (B = #BF)
;         RU: Выбрать поток #C1: дальше байты пакетов в #BFFD (B = #BF)
Ayfx_Stream
        ld b,high AYFX_PORT_REG
        ld a,AYFX_STREAM
        out (c),a
        ld b,high AYFX_PORT_DAT
        ret

;-------- EN: Is the card on the selected chip? Key, INFO, read #C1: "TRIN"? close. -> A = 1, NZ - yes; A = 0, Z - no
;         RU: Есть ли карта на выбранном чипе? Ключ, INFO, чтение #C1: "TRIN"? закрыть. -> A = 1, NZ - есть; A = 0, Z - нет
Ayfx_Detect
        push hl
        push de
        call Ayfx_Key
        call Ayfx_Stream
        ld a,AYFX_P_INFO
        out (c),a
        ld b,high AYFX_PORT_REG     ; EN: read #FFFD = selected #C1   RU: чтение #FFFD - выбранный #C1
        ld hl,Ayfx_Sign
        ld de,4*256+1               ; EN: D - letters, E - result   RU: D - букв, E - итог
.r      in a,(c)
        cp (hl)
        jr z,$+4
        ld e,0                      ; EN: not the letter - no card   RU: не та буква - карты нет
        inc hl
        dec d
        jr nz,.r
        in a,(c)                    ; EN: 5th byte - version (read the answer out)   RU: 5-й байт - версия (дочитать)
        call Ayfx_Close
        ld a,e
        or a
        pop de
        pop hl
        ret
Ayfx_Sign       db "TRIN"

;-------- EN: Read INFO into HL (AYFX_INFO_N bytes: "TRIN", protocol version): key, stream, INFO, reads #C1, close.
;             HL - after the last byte. Spoils A, B, D
;         RU: Прочитать INFO в HL (AYFX_INFO_N байт: "TRIN", версия протокола): ключ, поток, INFO, чтения #C1, закрыть.
;             HL - за последним байтом. Портит A, B, D
Ayfx_Info
        call Ayfx_Key
        call Ayfx_Stream
        ld a,AYFX_P_INFO
        out (c),a
        ld b,high AYFX_PORT_REG     ; EN: read #FFFD = selected #C1   RU: чтение #FFFD - выбранный #C1
        ld d,AYFX_INFO_N
.r      in a,(c)
        ld (hl),a
        inc hl
        dec d
        jr nz,.r
        jp Ayfx_Close

;-------- EN: Card to default: MODE AY - stops the self-player (Atari etc.), the chip is Spectrum's again. Call it at
;             player init. -> A = 0 done, #FF - not allowed / no card (no answer for ~65536 status reads). Spoils D, E
;         RU: Карту в состояние по умолчанию: MODE AY - стоп самоплеера (Atari и т.п.), чип снова у Спектрума. Звать
;             при инициализации плеера. -> A = 0 сделано, #FF - нельзя / карты нет (нет ответа ~65536 чтений статуса). Портит D, E
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
        ld b,high AYFX_PORT_REG     ; EN: select #C0 - reads give the status byte   RU: выбрать #C0 - чтение даёт статус
        ld a,AYFX_CTL
        out (c),a
        ld de,0                     ; EN: timeout counter   RU: счётчик таймаута
.w      in a,(c)
        bit AYFX_ST_BUSY,a          ; EN: still parsing - wait   RU: ещё разбирает - ждать
        jr nz,.n
        bit AYFX_ST_REPLY,a         ; EN: answer ready - read it   RU: ответ готов - читать
        jr nz,.r
.n      dec de
        ld a,d
        or e
        jr nz,.w
        call Ayfx_Close             ; EN: no answer - no card   RU: ответа нет - карты нет
        ld a,#FF
        ret
.r      ld a,AYFX_STREAM            ; EN: answer: length 2, result   RU: ответ: длина 2, итог
        out (c),a
        in a,(c)
        in a,(c)
        in e,(c)
        call Ayfx_Close
        ld a,e
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
