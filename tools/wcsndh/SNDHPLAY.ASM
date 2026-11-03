; =========================================================================
; SNDH.WMF - плагин Wild Commander: музыка Atari ST (SNDH) на карте ZX Trinity, сервис 1 «плеер»
; (Trinity doc/trinity-sndh.md, doc/trinity-api.md п.5). Классический WMF (версия #10, расширение SND - короткое имя
; *.sndh) и только API оригинала WC v1.10: идёт и в оригинале, и в WC Improved (todo п.73).
; Файл - в слот карты 'SNDH' (SLOT_WRITE по 512), PLAY; окно: песня, время, прогресс, нагрузка карты.
; Клавиши: Space - пауза, <- / -> - перемотка 10 с, Up / Down - песня, PgUp / PgDn - файл, Esc - выход (стоп).
; С картой - только через SDK (sdk/trinity_ay.asm, Ayfx_Svc).
; =========================================================================
        device zxspectrum128

WLD             equ #6006               ; диспетчер API WC
MAX_SIZE        equ 120*1024            ; файл до: пул слотов карты 128К, из них лог задания 8К
WIN_W           equ 40                  ; окно: ширина с рамкой
LINE_W          equ WIN_W-2             ;   строка текста
BAR_W           equ 36                  ;   полоса прогресса
POLL_FRAMES     equ 10                  ; опрос карты - раз в столько кадров
SEEK_FRAMES     equ 500                 ; перемотка - 10 с (кадр 20 мс)
SONG_WAIT       equ 3                   ; смена песни: кадров между STOP и PLAY (карта отпускает режим)

; --- API WC (номера оригинала v1.10)
API_PRWOW       equ 1
API_RRESB       equ 2
API_PRSRW       equ 3
API_GEDPL       equ 15
API_SPACE       equ 16
API_UP          equ 17
API_DOWN        equ 18
API_LEFT        equ 19
API_RIGHT       equ 20
API_ESC         equ 23
API_PGUP        equ 25
API_PGDN        equ 26
API_UNPRESS     equ 46
API_ANYKEY      equ 47
API_LOAD512     equ 48
API_GIPAGPL     equ 50

; --- выход в WC
RET_OK          equ 0
RET_NEXT        equ 2                   ; следующий файл
RET_PREV        equ 4                   ; предыдущий файл

; --- заголовок WMF: 512 байт
        org 0
Head    ds 16,0
        db "WildCommanderMDL"
        db #10,0                        ; версия формата, флаги
        db 1,0                          ; страниц, какая в #8000
        db 0,(CodeEnd-Code+511)/512     ; блок: страница 0, секторов
        ds 5*2,0
        ds 15,0
        db 0                            ; Enter и F3
        db "SND"                        ; расширение 0
        ds 31*3,0
        db 0
        dd #FFFFFFFF                    ; размер - проверяем сами (сообщение)
.name   db "Trinity SNDH player"
        ds 32-($-.name)," "
        db 0                            ; запуск по расширению
        ds 6+24+32+252,0
        assert $ == 512

        disp #8000
; ================= вход: DE:HL - размер файла, BC - имя =================
Code    push ix
        ld (Size),hl
        ld (Size+2),de
        call Name_Copy                  ; имя - сразу: GEDPL портит память WC
        call Main
        pop ix
        ret

; Имя файла (BC, до 0) -> строка 1 окна
Name_Copy
        ld hl,Line1
        ld d,LINE_W
.c      ld a,(bc)
        or a
        ret z
        ld (hl),a
        inc bc
        inc hl
        dec d
        jr nz,.c
        ret

Main    ld a,API_GEDPL                  ; текстовый экран WC
        call WLD
        ld ix,Win
        ld a,API_PRWOW
        call WLD
        ld ix,Win
        ld hl,Title                     ; заголовок - на рамке
        ld de,0*256+(WIN_W-TITLE_N)/2
        ld bc,TITLE_N
        ld a,API_PRSRW
        call WLD
        ld hl,TxLoad
        call State_Set
        call Draw
        call Size_Check                 ; ошибки: HL - текст
        jr c,Fail
        call Card_Find
        jr c,Fail
        call Load
        jr c,Fail
        ld a,1                          ; песня 1
        ld (Song),a
        call Play
        jr c,Fail
        jp Run

; Ошибка: текст HL - в строку состояния, ждать клавишу, выход
Fail    call State_Set
        call Draw
        ld a,API_UNPRESS
        call WLD
        ld a,API_ANYKEY
        call WLD
        ld a,RET_OK
        jp Quit

; ================= проверки и загрузка =================

; Размер: 1..MAX_SIZE -> CF = 1, HL - текст ошибки
Size_Check
        ld hl,(Size+2)
        ld a,h
        or a
        jr nz,.big
        ld a,l
        cp 2
        jr nc,.big
        ld hl,(Size)
        or a
        jr nz,.hi                       ; старшее слово 1
        ld a,h
        or l
        ret nz
        ld hl,TxEmpty
        scf
        ret
.hi     ld bc,(MAX_SIZE & #FFFF)+1
        or a
        sbc hl,bc
        ccf
        ret nc
.big    ld hl,TxBig
        scf
        ret

; Карта на чипе 0 -> CF = 1, HL - текст
Card_Find
        call Chip0
        call Ayfx_Detect
        ret nz
        ld hl,TxNoCard
        scf
        ret

; Слоты 'SNDH' (размер файла) и 'STAT', файл - по 512 в SNDH -> CF = 1, HL - текст
Load    ld hl,(Size)                    ; размер - в пакет SLOT_NEW
        ld (TSlotNew+8),hl
        ld hl,(Size+2)
        ld (TSlotNew+10),hl
        ld hl,TSlotNew
        call Req
        or a
        jp z,.room
        ld (SlotIn),a
        ld hl,TStatNew
        call Req
        or a
        jp z,.room
        ld (SlotSt),a
        ld a,API_GIPAGPL                ; поток - в начало файла
        call WLD
        ld hl,0
        ld (Off),hl
        ld (Off+2),hl
        ld hl,(Size)
        ld (Left),hl
        ld hl,(Size+2)
        ld (Left+2),hl
.blk    ld hl,Buf+8                     ; сектор - сразу на место данных пакета
        ld b,1
        ld a,API_LOAD512
        call WLD
        jp c,.io
        ld hl,(Left)                    ; n = min(512, осталось)
        ld de,512
        ld a,(Left+2)
        or a
        jr nz,.n512
        or a
        sbc hl,de
        add hl,de
        jr nc,.n512
        ex de,hl
.n512   ld (N),de
        ld hl,Buf                       ; SLOT_WRITE: код, длина 5+n, слот, смещение 4, байты
        ld (hl),AYFX_P_SLOT_WRITE
        inc hl
        ld a,e
        add a,5
        ld (hl),a
        inc hl
        ld a,d
        adc a,0
        ld (hl),a
        inc hl
        ld a,(SlotIn)
        ld (hl),a
        inc hl
        ld de,(Off)
        ld (hl),e
        inc hl
        ld (hl),d
        inc hl
        ld de,(Off+2)
        ld (hl),e
        inc hl
        ld (hl),d
        call Chip0
        ld hl,(N)                       ; пакет: 8 + n байт
        ld de,8
        add hl,de
        ex de,hl
        ld hl,Buf
        call Ayfx_Svc
        inc a
        jr z,.lost                      ; #FF - карта не ответила
        ld de,(N)                       ; смещение += n, осталось -= n
        ld hl,(Off)
        add hl,de
        ld (Off),hl
        jr nc,$+9
        ld hl,(Off+2)
        inc hl
        ld (Off+2),hl
        ld hl,(Left)
        or a
        sbc hl,de
        ld (Left),hl
        jr nc,$+9
        ld hl,(Left+2)
        dec hl
        ld (Left+2),hl
        ld hl,(Left)
        ld a,h
        or l
        ld hl,(Left+2)
        or h
        or l
        jp nz,.blk
        ret                             ; CF = 0
.room   ld hl,TxRoom
        scf
        ret
.io     ld hl,TxRead
        scf
        ret
.lost   ld hl,TxNoCard
        scf
        ret

; PLAY песни Song -> CF = 1, HL - текст
Play    xor a
        ld (Paused),a
        ld (St+ATARI_ST_FRAME),a        ; позиция - с начала, пока карта не ответила
        ld (St+ATARI_ST_FRAME+1),a
        ld a,(Song)
        ld e,a
        ld d,0
        ld a,ATARI_PLAY
        call Cmd
        or a
        ld hl,TxRefused
        scf
        ret nz
        ld a,1                          ; первый опрос - через кадр
        ld (Tick),a
        ld hl,TxPlay
        call State_Set
        or a
        ret

; ================= игра =================

Run     ei
        halt
        call Keys                       ; клавиша кончила игру - A = выход
        jp c,Quit
        ld hl,Tick
        dec (hl)
        jr nz,Run
        ld (hl),POLL_FRAMES
        call Poll
        ld a,(JState)
        cp SVC_JOB_RUN
        jr nz,.end
        call Track_End
        ld a,RET_NEXT
        jp c,Quit
        call Draw
        jr Run
.end    cp SVC_JOB_ERR                  ; ошибка - первая строка лога карты
        ld a,RET_OK
        jp nz,Quit                      ; готово: остановили не мы (MODE AY, сброс)
        call Log_Line
        jp Fail

; Конец песни: длина известна и позиция дошла -> CF = 1
Track_End
        ld hl,(St+ATARI_ST_FRAMES)
        ld de,(St+ATARI_ST_FRAMES+2)
        ld a,h
        or l
        or d
        or e
        ret z                           ; длины нет - играет по кругу
        ld bc,(St+ATARI_ST_FRAME)       ; позиция - длина: заём - ещё играет
        ld a,c
        sub l
        ld a,b
        sbc a,h
        ld bc,(St+ATARI_ST_FRAME+2)
        ld a,c
        sbc a,e
        ld a,b
        sbc a,d
        ccf
        ret

; Клавиши -> CF = 1, A - выход в WC
Keys    ld a,API_ESC
        call WLD
        ld a,RET_OK
        jr nz,.quit
        ld a,API_PGDN
        call WLD
        ld a,RET_NEXT
        jr nz,.quit
        ld a,API_PGUP
        call WLD
        ld a,RET_PREV
        jr nz,.quit
        ld a,API_SPACE
        call WLD
        jr nz,Pause
        ld a,API_LEFT
        call WLD
        ld hl,-SEEK_FRAMES
        jr nz,Seek
        ld a,API_RIGHT
        call WLD
        ld hl,SEEK_FRAMES
        jr nz,Seek
        ld a,API_UP
        call WLD
        ld a,-1
        jr nz,Song_Step
        ld a,API_DOWN
        call WLD
        ld a,1
        jr nz,Song_Step
        or a
        ret
.quit   scf
        ret

; Пауза / дальше
Pause   ld a,(Paused)
        xor 1
        ld (Paused),a
        ld e,a
        ld d,0
        ld a,ATARI_PAUSE
        call Cmd
        ld hl,TxPlay
        ld a,(Paused)
        or a
        jr z,$+5
        ld hl,TxPause
        call State_Set
        jr Key_Done

; Перемотка: позиция + HL кадров (не меньше 0; позиция - младшие 16 бит)
Seek    ld de,(St+ATARI_ST_FRAME)
        add hl,de
        bit 7,h                         ; ушло ниже 0 (позиция до 32767 кадров - 10 мин)
        jr z,$+5
        ld hl,0
        ld (St+ATARI_ST_FRAME),hl
        ex de,hl
        ld a,ATARI_SEEK
        call Cmd
        ld hl,TxSeek
        call State_Set
        ld a,1                          ; опрос - сразу
        ld (Tick),a
        jr Key_Done

; Песня Song + A (1..песен): STOP, пауза, PLAY
Song_Step
        ld hl,Song
        add a,(hl)
        jr z,Key_Done                   ; ниже первой
        ld c,a
        ld a,(St+ATARI_ST_SONG+1)
        cp c
        jr c,Key_Done                   ; выше последней
        ld (hl),c
        ld a,ATARI_STOP
        call Cmd
        ld b,SONG_WAIT
.w      ei
        halt
        djnz .w
        call Play
        jr nc,Key_Done
        pop af                          ; карта не взяла - ошибка (из Keys - в Run)
        jp Fail

; Клавишу отпустили - дальше; CF = 0
Key_Done
        call Draw
        ld a,API_UNPRESS
        call WLD
        or a
        ret

; Выход: STOP, окно убрать, слоты освободить. A - выход в WC
Quit    push af
        ld a,(SlotIn)                   ; файла на карте нет - и плеера нет (карты может не быть)
        or a
        ld a,ATARI_STOP
        call nz,Cmd
        call Cmd
        ld ix,Win
        ld a,API_RRESB
        call WLD
        ld a,(SlotIn)
        call Slot_Free
        ld a,(SlotSt)
        call Slot_Free
        pop af
        ret

; ================= карта (только через SDK) =================

; Чип 0 TurboSound выбран, C = #FD
Chip0   ld bc,AYFX_PORT_REG
        ld a,#FF
        out (c),a
        ret

; Служебный пакет по шаблону HL (длина, байты): копия в Buf, Ayfx_Svc -> HL - ответ, A - первый байт
Req     push hl
        call Chip0
        pop hl
        ld c,(hl)
        ld b,0
        inc hl
        push bc
        ld de,Buf
        ldir
        pop de
        ld hl,Buf
        ld c,low AYFX_PORT_REG
        jp Ayfx_Svc

; Команда плееру: A - команда, DE - параметр (младшие 16 бит) -> A - итог
Cmd     ld (TCall+5),a
        ld (TCall+8),de
        ld a,(SlotIn)
        ld (TCall+6),a
        ld a,(SlotSt)
        ld (TCall+7),a
        ld hl,TCall
        call Req
        ret

; Освободить слот A (0 - нет)
Slot_Free
        or a
        ret z
        ld (TFree+4),a
        ld hl,TFree
        jp Req

; Опрос: состояние задания, прогресс, слот STAT -> JState, JProg, St
Poll    ld hl,TJob
        call Req
        ld (JState),a
        inc hl
        ld a,(hl)
        ld (JProg),a
        ld a,(SlotSt)
        ld (TRead+4),a
        ld hl,TRead
        call Req
        ld a,d                          ; ответ не той длины - St не трогать
        or a
        ret nz
        ld a,e
        cp ATARI_ST_SIZE
        ret nz
        ld de,St
        ld bc,ATARI_ST_SIZE
        ldir
        ret

; Первая строка лога задания (что сказала карта) -> HL - текст (0 в конце)
Log_Line
        ld hl,TLog
        call Req
        ld bc,LogTx
        ld a,d                          ; длина ответа: больше строки - строка
        or a
        jr nz,.w
        ld a,e
        cp LINE_W
        jr c,.n
.w      ld e,LINE_W
.n      ld a,e
        or a
        jr z,.e
.c      ld a,(hl)
        cp 10
        jr z,.e
        ld (bc),a
        inc hl
        inc bc
        dec e
        jr nz,.c
.e      xor a
        ld (bc),a
        ld hl,LogTx
        ret

; ================= окно =================

; Строка состояния (5) - текст HL (0 в конце)
State_Set
        push hl
        ld hl,Line5
        call Line_Clear
        pop hl
        ld de,Line5
.c      ld a,(hl)
        or a
        ret z
        ld (de),a
        inc hl
        inc de
        jr .c

; Строку HL - пробелами
Line_Clear
        ld b,LINE_W
.c      ld (hl),' '
        inc hl
        djnz .c
        ret

; Окно целиком: строки 2..4 - из St / JProg, все 5 - на экран
Draw    ld hl,Line2                     ; 2: песня, время
        call Line_Clear
        ld hl,TxSong
        ld de,Line2
        ld bc,TX_SONG_N
        ldir
        ld a,(Song)
        ld l,a
        ld h,0
        ld de,Line2+TX_SONG_N+2
        ld b,3
        call DecS
        ld a,'/'
        ld (Line2+TX_SONG_N+3),a
        ld a,(St+ATARI_ST_SONG+1)
        ld l,a
        ld h,0
        ld de,Line2+TX_SONG_N+6
        ld b,3
        call DecS
        ld hl,(St+ATARI_ST_FRAME)
        ld de,(St+ATARI_ST_FRAME+2)
        ld ix,Line2+LINE_W-13
        call Time
        ld a,'/'
        ld (Line2+LINE_W-7),a
        ld hl,(St+ATARI_ST_FRAMES)
        ld de,(St+ATARI_ST_FRAMES+2)
        ld a,h
        or l
        or d
        or e
        ld ix,Line2+LINE_W-5
        jr z,.none
        call Time
        jr .bar
.none   ld hl,TxNoTime
        ld de,Line2+LINE_W-5
        ld bc,5
        ldir
.bar    ld hl,Line3                     ; 3: полоса прогресса BAR_W
        call Line_Clear
        ld a,(JProg)                    ; закрашено = прогресс * BAR_W / 100
        ld l,a
        ld h,0
        ld de,BAR_W
        call Mul
        ld c,100
        call Div8
        ld b,BAR_W
        ld de,Line3+(LINE_W-BAR_W)/2
.f      ld a,#DB                        ; ¦ - сыграно, ¦ - нет (CP866)
        dec l
        jp p,$+5
        ld a,#B0
        ld (de),a
        inc de
        djnz .f
        ld hl,Line4                     ; 4: нагрузка карты, пик, опоздания
        call Line_Clear
        ld hl,TxLoadN
        ld de,Line4
        ld bc,TX_LOADN_N
        ldir
        ld hl,(St+ATARI_ST_LOAD)
        ld de,Line4+8
        call Pct
        ld hl,(St+ATARI_ST_LOAD+2)
        ld de,Line4+19
        call Pct
        ld hl,(St+ATARI_ST_LATE)
        ld de,Line4+LINE_W-1
        ld b,5
        call DecS
        ld hl,Line1                     ; на экран: строки 1..5 с X = 1
        ld d,1
.p      push hl
        push de
        ld ix,Win
        ld e,1
        ld bc,LINE_W
        ld a,API_PRSRW
        call WLD
        pop de
        pop hl
        ld bc,LINE_W
        add hl,bc
        inc d
        ld a,d
        cp 6
        jr c,.p
        ret

; Время кадров DE:HL как ММ:СС в IX (5 знаков); больше 65535 кадров - 65535
Time    ld a,d
        or e
        jr z,$+5
        ld hl,#FFFF
        ld c,50                         ; кадры -> секунды
        call Div8
        ld c,60                         ; секунды -> минуты, A - секунды
        call Div8
        push hl
        ld l,a
        ld h,0
        push ix
        pop de
        inc de
        inc de
        inc de
        inc de
        ld b,2
        call DecZ
        ld (ix+2),':'
        pop hl
        push ix
        pop de
        inc de
        ld b,2
        jp DecZ

; Нагрузка: мкс HL из 20000 -> проценты (/200), 3 знака, последний - в DE-1, знак % - в DE
Pct     ld a,'%'
        ld (de),a
        dec de
        ld c,200
        call Div8
        ld b,3
        jp DecS

; ================= числа =================

; HL = HL / C, A - остаток (C до 255)
Div8    xor a
        ld b,16
.l      add hl,hl
        rla
        jr c,.s                         ; остаток вышел за байт - вычитать
        cp c
        jr c,.n
.s      sub c
        inc l
.n      djnz .l
        ret

; HL = HL * DE (младшие 16 бит)
Mul     ld b,h
        ld c,l
        ld hl,0
        ld a,16
.l      add hl,hl
        ex de,hl
        add hl,hl
        ex de,hl
        jr nc,.n
        add hl,bc
.n      dec a
        jr nz,.l
        ret

; Число HL справа налево: DE - последняя цифра, B - знаков; слева пробелы (DecS) / нули (DecZ)
DecZ    ld a,'0'
        jr Dec
DecS    ld a,' '
Dec     ld (.pad+1),a
.l      push bc
        ld c,10
        call Div8
        pop bc
        add a,'0'
        ld (de),a
        dec de
        dec b
        ret z
        ld a,h
        or l
        jr nz,.l
.pad    ld a,' '
.f      ld (de),a
        dec de
        djnz .f
        ret

; ================= данные =================

Win     db 128,0                        ; тень, обычная рамка
        db 255,255                      ; по центру
        db WIN_W,7
        db %01111111,0
        dw 0                            ; буфер фона - назначит WC
        db 0,0

Title   db " Trinity SNDH "
TITLE_N equ $-Title
TxSong  db "Song"
TX_SONG_N equ $-TxSong
TxLoadN db "Load    %  peak    %  late"
TX_LOADN_N equ $-TxLoadN
TxNoTime db "--:--"
TxLoad  db "Loading...",0
TxPlay  db "Playing   Space pause  <- -> seek  Esc",0
TxPause db "Paused    Space play",0
TxSeek  db "Seeking...",0
TxEmpty db "Empty file",0
TxBig   db "File too big (card slot: up to 120K)",0
TxNoCard db "Trinity card not found",0
TxRoom  db "No room on the card",0
TxRead  db "Disk read error",0
TxRefused db "The card refused to play",0

; Шаблоны пакетов: длина, байты (поля правятся перед Req)
TSlotNew db 12, AYFX_P_SLOT_NEW,9,0, "SNDH", 0,0,0,0, 0
TStatNew db 12, AYFX_P_SLOT_NEW,9,0, "STAT", ATARI_ST_SIZE,0,0,0, 0
TCall   db 11, AYFX_P_SVC_CALL,8,0, ATARI_SVC,0,0,0, 0,0,0,0
TJob    db 3, AYFX_P_JOB_STATUS,0,0
TLog    db 3, AYFX_P_JOB_LOG,0,0
TRead   db 10, AYFX_P_SLOT_READ,7,0, 0, 0,0,0,0, ATARI_ST_SIZE,0
TFree   db 4, AYFX_P_SLOT_FREE,1,0, 0

        include "sdk/trinity_ay.asm"

Size    dd 0                            ; размер файла
Off     dd 0                            ; загрузка: смещение в слоте
Left    dd 0                            ;   осталось
N       dw 0                            ;   байт в этом пакете
SlotIn  db 0                            ; слоты карты: файл, состояние (0 - нет)
SlotSt  db 0
Song    db 1
Paused  db 0
Tick    db 1                            ; кадров до опроса
JState  db 0                            ; задание: состояние, прогресс
JProg   db 0
St      ds ATARI_ST_SIZE,0              ; слот STAT
Line1   ds LINE_W," "                   ; строки окна: имя, песня и время, полоса, нагрузка, состояние
Line2   ds LINE_W," "
Line3   ds LINE_W," "
Line4   ds LINE_W," "
Line5   ds LINE_W," "
LogTx   ds LINE_W+1,0
Buf     ds 8+512+64,0                   ; пакет и ответ
CodeEnd
        ent
        assert CodeEnd < #C000

        savebin "SNDH.WMF",Head,512+(CodeEnd-Code+511)/512*512   ; целыми секторами
