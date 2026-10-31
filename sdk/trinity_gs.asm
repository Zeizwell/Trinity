; =========================================================================
; ZX Trinity RP-GS driver - API for Z80 programs / Драйвер RP-GS - API для программ Z80
; =========================================================================
; EN: Data and settings of the card, no UI: include this file and call. Any Spectrum: only ports #BB / #B3,
;     no pages, no own buffers, no fixed address. Not for players: they use the GS as usual.
; RU: Данные и настройки карты, без интерфейса: подключил файл и вызываешь. Любой Спектрум: только порты #BB / #B3,
;     без страниц, своих буферов и постоянного адреса. Не для плееров: они работают с GS как обычно.
;
; QUICK START / КАК НАЧАТЬ
;
;       call Gsc_Detect         ; EN: first of all: is the card here?   RU: первым делом: карта есть?
;       jr nc,NoCard
;       ld hl,TagMaster
;       ld de,Value
;       call Gsc_Cfg_Set        ; EN: volume 50 %                       RU: громкость 50 %
;       jr nc,Error
;       call Gsc_Close          ; EN: at the end                        RU: в конце
;       ...
; TagMaster db "MAST"
; Value     db 50,0,0,0
;
; RULES / ПРАВИЛА
;   1. EN: Gsc_Detect first, Gsc_Close last. Any plain GS command closes the channel too: then Gsc_Detect again.
;      RU: Первым Gsc_Detect, последним Gsc_Close. Любая обычная команда GS тоже закрывает канал: тогда снова Gsc_Detect.
;   2. EN: Every routine answers the same way:  C - ok;  NC - error, A = error code (GSC_E_*).
;      RU: Все процедуры отвечают одинаково:     C - успех;  NC - ошибка, A = код ошибки (GSC_E_*).
;   3. EN: Numbers are little endian. Interrupts may stay on.
;      RU: Числа - младшим байтом вперёд. Прерывания можно не запрещать.
;    4. EN: Finish a GS load (#30 / #38 ... #D2) before Gsc_Detect: during a load the key bytes go into the module and spoil it, the channel stays closed. After #D2 all is well, reload the module.
;         RU: Закончить загрузку в GS (#30 / #38 ... #D2) до Gsc_Detect: во время загрузки байты ключа попадут в модуль и испортят его, канал не откроется. После #D2 всё работает, модуль загрузить заново.
;
; ROUTINES / ПРОЦЕДУРЫ
;   Gsc_Detect      EN: is the card here, open the channel      RU: есть ли карта, открыть канал
;   Gsc_Close       EN: close the channel                       RU: закрыть канал
;   Gsc_Version     EN: protocol and firmware versions          RU: версии протокола и прошивки
;   Gsc_Cfg_Get     EN: read a setting                          RU: прочитать настройку
;   Gsc_Cfg_Set     EN: change a setting                        RU: поменять настройку
;   Gsc_Preset      EN: choose a preset                         RU: выбрать пресет
;   Gsc_Save        EN: keep settings in flash                  RU: сохранить настройки во флеш
;   Gsc_Stat        EN: load, peak, late blocks...              RU: нагрузка, пик, опоздания...
;   Gsc_Stat_Reset  EN: peak and late blocks from zero          RU: пик и опоздания с нуля
;   Gsc_Curve       EN: filter curve                            RU: кривая фильтров
;
; SETTINGS / НАСТРОЙКИ: tag 4 chars / тег 4 знака, value 4 bytes / значение 4 байта
;   Tag     EN                              RU                              Limits / Пределы
;   "MODE"  mode, 4 - custom, read only     режим, 4 - Custom, только чтение
;   "RATE"  output rate, Hz, read only      частота вывода, Гц, только чтение
;   "SYSK"  clock, kHz, read only           такт, кГц, только чтение
;   "OVER"  oversampling, read only         передискретизация, только чтение
;   "MAST"  volume, %                       громкость, %                    0..100
;   "BASS"  bass, dB                        НЧ, дБ                          -12..12
;   "TREB"  treble, dB                      ВЧ, дБ                          -12..12
;   "RAMK"  GS memory, KB                   память GS, КБ                   128..8192
;   "GSON"  GS sounds                       GS звучит                       0..1
;   EN: settings below switch the mode to custom    RU: настройки ниже переводят режим в Custom
;   "VOIC"  voices                          голосов                         4..32
;   "RSMP"  resampling: 0 steps, 1 BLEP,    пересчёт: 0 ступени, 1 BLEP,    0..4
;           2 linear, 3 GS ROM, 4 cubic     2 линейная, 3 ROM GS, 4 кубика
;   "HOLD"  tick rate, Hz                   частота тиков, Гц               8000..75000
;   "PWM "  PWM volume                      громкость ШИМ                   0..1
;   "RAMP"  smooth volume                   плавная громкость               0..1
;   "COMP"  compressor                      компрессор                      0..1
;   "STER"  stereo width, %                 ширина стерео, %                0..100
;   "LED "  LED: 0 off, 1 by E0x, 2 on      LED: 0 выкл, 1 по E0x, 2 вкл    0..2
;   "CBAS"  bass colour, dB                 окраска НЧ, дБ                  -12..12
;   "CTRE"  treble colour, dB               окраска ВЧ, дБ                  -12..12
;   "RCHZ" "LPHZ" "LDHZ" "HPHZ"             срез RC, ФНЧ, LED, ФВЧ          100..2400000
;           cutoff, 0.01 Hz                 в 0.01 Гц
;   "LDQ "  LED Q, 0.001                    добротность LED, 0.001          100..10000
;   "RCON" "LPON" "HPON"  stage on          ступень включена                0..1
; =========================================================================

; --- Errors, A after NC / Ошибки, A при NC
GSC_E_TIMEOUT   equ 1               ; EN: no answer: no card, plain GS, channel closed
                                    ; RU: нет ответа: нет карты, обычный GS, канал закрыт
GSC_E_REFUSED   equ #FF             ; EN: card said no: no such tag, out of limits, read only, bad number
                                    ; RU: карта отказала: нет тега, вне пределов, только чтение, неверный номер

; --- Presets, A for Gsc_Preset / Пресеты, A для Gsc_Preset
GSC_MODE_GS     equ 0
GSC_MODE_A500   equ 1
GSC_MODE_A1200  equ 2
GSC_MODE_HIFI   equ 3

; --- Curve stages, A for Gsc_Curve / Ступени кривой, A для Gsc_Curve
GSC_CV_RC       equ 0
GSC_CV_LP       equ 1
GSC_CV_LED      equ 2
GSC_CV_HP       equ 3
GSC_CV_BASS     equ 4
GSC_CV_TREBLE   equ 5
GSC_CV_ALL      equ #FF             ; EN: the total     RU: итог
GSC_CV_SIZE     equ 64              ; EN: 32 signed words, 0.01 dB, 20 * 1000^(i/31) Hz
                                    ; RU: 32 слова со знаком, 0.01 дБ, 20 * 1000^(i/31) Гц

; --- Gsc_Version answer, offsets / Ответ Gsc_Version, смещения
GSC_VER_PROTO   equ 0               ; EN: protocol, 1 byte          RU: протокол, 1 байт
GSC_VER_FW      equ 1               ; EN: firmware, 2 bytes         RU: прошивка, 2 байта
GSC_VER_SIZE    equ 3

; --- Gsc_Stat answer, offsets / Ответ Gsc_Stat, смещения
GSC_ST_LOAD     equ 0               ; EN: load, 2 bytes, 0.1 %      RU: нагрузка, 2 байта, 0.1 %
GSC_ST_TOP      equ 2               ; EN: peak, 2 bytes, 0.1 %      RU: пик, 2 байта, 0.1 %
GSC_ST_LATE     equ 4               ; EN: late blocks, 4 bytes      RU: опоздания, 4 байта
GSC_ST_BLOCKS   equ 8               ; EN: blocks, 4 bytes           RU: блоки, 4 байта
GSC_ST_RATE     equ 12              ; EN: output rate, Hz, 4 bytes  RU: частота вывода, Гц, 4 байта
GSC_ST_SYSK     equ 16              ; EN: clock, kHz, 4 bytes       RU: такт, кГц, 4 байта
GSC_ST_MODE     equ 20              ; EN: mode, 1 byte, 4 - custom  RU: режим, 1 байт, 4 - Custom
GSC_ST_DEV      equ 21              ; EN: device, 4 chars           RU: устройство, 4 знака
GSC_ST_SIZE     equ 25

; --- Ports and commands, not needed by programs / Порты и команды, программам не нужны.
;     EN: numbers only here and in engine/gs/gs_ctl.h   RU: номера только здесь и в engine/gs/gs_ctl.h
GSC_PORT_CMD    equ #BB             ; EN: write command, read status (bit 7 data, bit 0 command)
                                    ; RU: запись - команда, чтение - статус (бит 7 данные, бит 0 команда)
GSC_PORT_DAT    equ #B3             ; EN: data     RU: данные
GSC_KEY         equ #FB             ; EN: one key byte in #B3    RU: один байт ключа в #B3
GSC_BYTE        equ #FC             ; EN: one packet byte in #B3 RU: один байт пакета в #B3
GSC_CLOSE       equ #FD             ; EN: close the channel      RU: закрыть канал
GSC_P_CFG_GET   equ #D0
GSC_P_CFG_SET   equ #D1
GSC_P_CFG_SAVE  equ #D2
GSC_P_PRESET    equ #D3
GSC_P_STAT      equ #D4
GSC_P_STAT_RES  equ #D5
GSC_P_CURVE     equ #D6
GSC_P_VERSION   equ #D7

; =========================== API ===========================

;-------------------------------------------------------------------------
; Gsc_Detect - EN: is the card here? Call first, it opens the channel.
;              RU: есть ли карта? Вызывать первым, открывает канал.
; In:     -
; Out:    C  - EN: our card, A = protocol version       RU: наша карта, A = версия протокола
;         NC - EN: no card or a plain GS, A = GSC_E_TIMEOUT
;              RU: карты нет или обычный GS, A = GSC_E_TIMEOUT
; Spoils: A, BC, DE, HL
; Time:   EN: without the card about 1 s    RU: без карты около 1 с
;-------------------------------------------------------------------------
Gsc_Detect
        ld hl,Gsc_Sign
        ld d,Gsc_Sign_N
        ld e,GSC_KEY
Gsc_Detect_Key
        ld a,(hl)
        inc hl
        call Gsc_Send
        ret nc
        dec d
        jr nz,Gsc_Detect_Key
        jp Gsc_Get
Gsc_Sign        db "TRINITY"
Gsc_Sign_N      equ $-Gsc_Sign

;-------------------------------------------------------------------------
; Gsc_Close - EN: close the channel, the card is a plain GS again. Always succeeds.
;             RU: закрыть канал, карта снова обычный GS. Всегда успешно.
; In:     -
; Out:    C
; Spoils: A
;-------------------------------------------------------------------------
Gsc_Close
        ld a,GSC_CLOSE
        out (GSC_PORT_CMD),a
        scf
        ret

;-------------------------------------------------------------------------
; Gsc_Version - EN: versions of the protocol and firmware.
;               RU: версии протокола и прошивки.
; In:     DE - EN: buffer, GSC_VER_SIZE bytes       RU: буфер, GSC_VER_SIZE байт
; Out:    C  - EN: buffer filled, offsets GSC_VER_* RU: буфер заполнен, смещения GSC_VER_*
;         NC - EN: A = error                        RU: A = ошибка
; Spoils: A, BC, DE, HL
;-------------------------------------------------------------------------
Gsc_Version
        ld a,GSC_P_VERSION
        ld c,0
        call Gsc_Head
        ret nc
        call Gsc_Answer
        ret nc
        ld a,l
        cp GSC_VER_SIZE
        jr Gsc_Size_Check

;-------------------------------------------------------------------------
; Gsc_Cfg_Get - EN: read a setting.
;               RU: прочитать настройку.
; In:     HL - EN: tag, 4 chars                     RU: тег, 4 знака
;         DE - EN: buffer for the value, 4 bytes    RU: буфер под значение, 4 байта
; Out:    C  - EN: value in the buffer              RU: значение в буфере
;         NC - EN: A = error, the buffer is spoiled RU: A = ошибка, буфер испорчен
; Spoils: A, BC, DE, HL
;-------------------------------------------------------------------------
Gsc_Cfg_Get
        ld a,GSC_P_CFG_GET
        ld c,4
        call Gsc_Head
        ret nc
        ld b,4
        call Gsc_Put_N
        ret nc
        call Gsc_Answer
        ret nc
        ld a,l
        cp 4
        jr Gsc_Size_Check

;-------------------------------------------------------------------------
; Gsc_Cfg_Set - EN: change a setting. Sound changes at once, flash only by Gsc_Save.
;               RU: поменять настройку. Звук меняется сразу, флеш только через Gsc_Save.
; In:     HL - EN: tag, 4 chars                     RU: тег, 4 знака
;         DE - EN: value, 4 bytes                   RU: значение, 4 байта
; Out:    C  - EN: done                             RU: сделано
;         NC - EN: A = error                        RU: A = ошибка
; Spoils: A, BC, DE, HL
;-------------------------------------------------------------------------
Gsc_Cfg_Set
        ld a,GSC_P_CFG_SET
        ld c,8
        call Gsc_Head
        ret nc
        ld b,4
        call Gsc_Put_N
        ret nc
        ex de,hl
        ld b,4
        call Gsc_Put_N
        ret nc
        jr Gsc_Ack

;-------------------------------------------------------------------------
; Gsc_Preset - EN: choose a preset. Sound changes at once, flash only by Gsc_Save.
;              RU: выбрать пресет. Звук меняется сразу, флеш только через Gsc_Save.
; In:     A  - GSC_MODE_*
; Out:    C  - EN: done                             RU: сделано
;         NC - EN: A = error                        RU: A = ошибка
; Spoils: A, BC, L
;-------------------------------------------------------------------------
Gsc_Preset
        ld l,a
        ld a,GSC_P_PRESET
        ld c,1
        call Gsc_Head
        ret nc
        ld a,l
        call Gsc_Put
        ret nc
        jr Gsc_Ack

;-------------------------------------------------------------------------
; Gsc_Save - EN: keep the settings in flash. Sound stops for about 15 ms.
;            RU: сохранить настройки во флеш. Звук прерывается примерно на 15 мс.
; In:     -
; Out:    C  - EN: saved                            RU: сохранено
;         NC - EN: A = error                        RU: A = ошибка
; Spoils: A, BC
;-------------------------------------------------------------------------
Gsc_Save
        ld a,GSC_P_CFG_SAVE
        ld c,0
        call Gsc_Head
        ret nc
        jr Gsc_Ack

;-------------------------------------------------------------------------
; Gsc_Stat - EN: load of the card, for any device playing. Average is over the last 0.5 s.
;            RU: нагрузка карты, какое бы устройство ни играло. Среднее за последние 0.5 с.
; In:     DE - EN: buffer, GSC_ST_SIZE bytes        RU: буфер, GSC_ST_SIZE байт
; Out:    C  - EN: buffer filled, offsets GSC_ST_*  RU: буфер заполнен, смещения GSC_ST_*
;         NC - EN: A = error                        RU: A = ошибка
; Spoils: A, BC, DE, HL
;-------------------------------------------------------------------------
Gsc_Stat
        ld a,GSC_P_STAT
        ld c,0
        call Gsc_Head
        ret nc
        call Gsc_Answer
        ret nc
        ld a,l
        cp GSC_ST_SIZE
        jr Gsc_Size_Check

;-------------------------------------------------------------------------
; Gsc_Stat_Reset - EN: peak and late blocks from zero. Changing a setting or preset does the same.
;                  RU: пик и опоздания с нуля. То же делает смена настройки или пресета.
; In:     -
; Out:    C  - EN: done                             RU: сделано
;         NC - EN: A = error                        RU: A = ошибка
; Spoils: A, BC
;-------------------------------------------------------------------------
Gsc_Stat_Reset
        ld a,GSC_P_STAT_RES
        ld c,0
        call Gsc_Head
        ret nc
        jr Gsc_Ack

;-------------------------------------------------------------------------
; Gsc_Curve - EN: filter curve at the current settings.
;             RU: кривая фильтров при текущих настройках.
; In:     A  - GSC_CV_*
;         DE - EN: buffer, GSC_CV_SIZE bytes        RU: буфер, GSC_CV_SIZE байт
; Out:    C  - EN: buffer filled                    RU: буфер заполнен
;         NC - EN: A = error                        RU: A = ошибка
; Spoils: A, BC, DE, HL
;-------------------------------------------------------------------------
Gsc_Curve
        ld l,a
        ld a,GSC_P_CURVE
        ld c,1
        call Gsc_Head
        ret nc
        ld a,l
        call Gsc_Put
        ret nc
        call Gsc_Answer
        ret nc
        ld a,l
        cp GSC_CV_SIZE
        jr Gsc_Size_Check

; =========================== Inside, do not call / Внутреннее, не вызывать ===========================

; EN: After cp: answer of the wrong size means the card refused    RU: После cp: ответ другого размера - карта отказала
Gsc_Size_Check
        scf
        ret z
Gsc_Refused
        ld a,GSC_E_REFUSED
        or a
        ret

; EN: Answer of one byte, 0 - ok    RU: Ответ из одного байта, 0 - успех
Gsc_Ack
        call Gsc_Get
        ret nc
        call Gsc_Get
        ret nc
        call Gsc_Get
        ret nc
        or a
        jr nz,Gsc_Refused
        scf
        ret

; EN: Packet header: A = code, C = data length. Keeps DE, HL    RU: Шапка пакета: A = код, C = длина данных. Сохраняет DE, HL
Gsc_Head
        push bc
        call Gsc_Put
        pop bc
        ret nc
        ld a,c
        call Gsc_Put
        ret nc
        xor a
        jr Gsc_Put

; EN: B bytes from HL into the packet. Keeps DE    RU: B байт из HL в пакет. Сохраняет DE
Gsc_Put_N
        ld a,(hl)
        inc hl
        push bc
        call Gsc_Put
        pop bc
        ret nc
        djnz Gsc_Put_N
        scf
        ret

; EN: A = one packet byte. Keeps DE, HL    RU: A = байт пакета. Сохраняет DE, HL
Gsc_Put
        push de
        ld e,GSC_BYTE
        call Gsc_Send
        pop de
        ret

; EN: A = byte: into #B3, command E, wait until the card takes it    RU: A = байт: в #B3, команда E, ждём, пока карта возьмёт
Gsc_Send
        out (GSC_PORT_DAT),a
        ld a,e
        out (GSC_PORT_CMD),a
        ld bc,0
Gsc_Send_Wait
        in a,(GSC_PORT_CMD)
        rrca
        jr nc,Gsc_Send_Taken
        dec bc
        ld a,b
        or c
        jr nz,Gsc_Send_Wait
        ld a,GSC_E_TIMEOUT          ; EN: or c left NC    RU: or c оставил NC
        ret
Gsc_Send_Taken
        scf
        ret

; EN: Wait for data and read it -> A    RU: Дождаться данных и прочитать -> A
Gsc_Get
        ld bc,0
Gsc_Get_Wait
        in a,(GSC_PORT_CMD)
        rlca
        jr c,Gsc_Get_Ready
        dec bc
        ld a,b
        or c
        jr nz,Gsc_Get_Wait
        ld a,GSC_E_TIMEOUT          ; EN: or c left NC    RU: or c оставил NC
        ret
Gsc_Get_Ready
        in a,(GSC_PORT_DAT)
        scf
        ret

; EN: Answer [length 2 bytes][data] to DE -> HL = length    RU: Ответ [длина 2 байта][данные] в DE -> HL = длина
Gsc_Answer
        call Gsc_Get
        ret nc
        ld l,a
        call Gsc_Get
        ret nc
        ld h,a
        push hl
Gsc_Answer_Read
        ld a,h
        or l
        jr z,Gsc_Answer_End
        call Gsc_Get
        jr nc,Gsc_Answer_Fail
        ld (de),a
        inc de
        dec hl
        jr Gsc_Answer_Read
Gsc_Answer_End
        pop hl
        scf
        ret
Gsc_Answer_Fail
        pop hl                      ; EN: pop keeps NC and A    RU: pop не трогает NC и A
        ret
