; GSBUSTEST - проверка шины Спектрум <-> RP-GS карты Trinity через порты GS #BB / #B3 (TS-Config, запуск из WC).
; Шлюз секторов на тестовом диске в ОЗУ карты (SD не нужна): по кругу пишем 8 секторов узора, читаем их дважды
; и сверяем. Узор каждый проход свой (Seed). Ошибки:
;   WRITE - оба чтения одинаково не совпали с узором: испорчено при записи, WRITE OR - какие биты бывали плохими;
;   SKIP / DUP / BITS - чтение разошлось: байт пропал / повторился / другие биты (BITS OR - какие);
;   NO REPLY - карта не ответила (вызов SDK вернул NC): шлюз закрываем и открываем заново. Чтение оборвалось -
;              пришедшее сверяем: пропавший байт сдвигает хвост, и поток кончается на байт раньше (SKIP + NO REPLY).
; Частота процессора - клавиши 1 / 2 / 3 (3.5 / 7 / 14 МГц), Space - выход (шлюз закрыть: GS перезапустится).
; Карта - только через SDK (sdk/trinity_gs.asm). Порты TS-Config: VCONFIG #00AF, VPAGE #01AF, PAGE1 #11AF, SYSCONFIG #20AF.

        DEVICE NOSLOT64K
        org #8000

SECS    equ 8                           ; секторов за проход
N       equ SECS*512                    ; байт за проход
SHOW    equ 4                           ; экран - раз в столько проходов

start   di
        ld sp,#7FFF
        ld bc,#00AF : xor a : out (c),a         ; видео: ZX 256x192
        ld bc,#01AF : ld a,5 : out (c),a        ; экран - страница 5
        ld bc,#11AF : ld a,5 : out (c),a        ; окно #4000 - страница 5
        xor a : out (#FE),a
        ld hl,#4000 : ld de,#4001 : ld bc,#17FF : ld (hl),0 : ldir
        ld hl,#5800 : ld de,#5801 : ld bc,#02FF : ld (hl),#07 : ldir

        ld hl,t_head : ld de,0 : call print_at
        ld a,2 : call Speed                     ; начинаем с 14 МГц
        call Open
        ld hl,t_nocard
        jp nc,Stop
        call Labels

; ---- круг: узор, запись, два чтения, сверка, экран, клавиши
Loop    call Pat_Make
        call Pass
        ld hl,C_pass
        call Inc32
        ld hl,Seed
        inc (hl)
        ld a,(Cnt_S)
        inc a
        ld (Cnt_S),a
        cp SHOW
        jr c,.key
        xor a
        ld (Cnt_S),a
        call Show
.key    ld a,#F7 : in a,(#FE)                   ; 1 / 2 / 3 - частота
        bit 0,a : ld b,0 : jr z,.spd
        bit 1,a : ld b,1 : jr z,.spd
        bit 2,a : ld b,2 : jr z,.spd
        ld a,#7F : in a,(#FE)                   ; Space - выход
        rra
        jr c,Loop
        call Show
        call Gsc_Sd_Close
        ld hl,t_done
        jr Stop
.spd    ld a,b
        call Speed
        jr Loop

; Конец: текст HL внизу
Stop    ld de,23*256+0
        call print_at
.end    jr .end

; ---- карта (SDK)

; Канал и тестовый диск -> C - открыт
Open    call Gsc_Detect
        ret nc
        ld a,GSC_SD_TEST
        jp Gsc_Sd_Open

; Проход: запись узора, два чтения, сверка. Нет ответа - счётчик и шлюз заново
Pass    ld a,(Seed)                             ; сектора 0..7 или 8..15 - через проход
        and 1
        rrca
        rrca
        rrca
        rrca
        rrca                                    ; 0 или 8
        ld e,a
        ld d,0
        ld (Sec),de
        ld bc,0
        ld hl,Pat
        ld a,SECS
        call Gsc_Sd_Write
        jr nc,.norep
        ld de,(Sec)
        ld bc,0
        ld hl,Rbuf1
        ld a,SECS
        call Gsc_Sd_Read
        ld de,Rbuf1
        jr nc,.rfail
        ld de,(Sec)
        ld bc,0
        ld hl,Rbuf2
        ld a,SECS
        call Gsc_Sd_Read
        ld de,Rbuf2
        jr nc,.rfail
        ld hl,N
        ld (Len),hl
        jp Judge
.rfail  or a                                    ; чтение оборвалось: HL - куда лёг бы следующий байт
        sbc hl,de
        ld (Len),hl                             ; сверяем только пришедшее
        ex de,hl
        call Check
.norep  ld hl,C_norep
        call Inc32
        call Gsc_Sd_Close                       ; шлюз в исходное (GS перезапустится)
        jp Open                                 ; не открылось - следующий проход попробует снова

; Сверка прохода: чтения равны и не как узор - запись; иначе каждое чтение отдельно
Judge   ld hl,Rbuf1
        ld de,Rbuf2
        call Same
        jr nz,.reads
        ld hl,Rbuf1                             ; чтения равны: с узором?
        call Diff
        ret z
        ld a,(Got)                              ; запись: биты - в маску, счётчик, первая ошибка "W"
        ld hl,Exp
        xor (hl)
        ld hl,Wbits_Or
        or (hl)
        ld (hl),a
        ld hl,C_write
        ld c,"W"
        jp Count
.reads  ld hl,Rbuf1
        call Check
        ld hl,Rbuf2
        jp Check

; Два буфера чтения равны? HL, DE -> Z - равны
Same    ld bc,N
.s      ld a,(de)
        cp (hl)
        ret nz
        inc hl
        inc de
        dec bc
        ld a,b
        or c
        jr nz,.s
        ret

; Буфер HL с узором: Z - совпал; NZ - первое расхождение: Bad_At, Exp, Got; DE - в узоре
Diff    ld (Buf),hl
        ld de,Pat
        ld bc,(Len)
        ld a,b
        or c
        ret z                                   ; ничего не пришло - сверять нечего
.c      ld a,(de)
        cp (hl)
        jr nz,.bad
        inc hl
        inc de
        dec bc
        ld a,b
        or c
        jr nz,.c
        ret
.bad    ld (Bad_At),hl
        ld a,(hl)
        ld (Got),a
        ld a,(de)
        ld (Exp),a
        ret                                     ; NZ от cp

; Чтение HL: расхождение - по виду в счётчик (как BUSTEST)
Check   call Diff
        ret z
        ld hl,(Bad_At)
        inc de                                  ; следующий байт узора = пришедший - пропуск
        ld a,(de)
        cp (hl)
        ld hl,C_skip
        ld c,"S"
        jr z,Count
        dec de                                  ; прошлый байт узора = пришедший - повтор (не на первом)
        ld hl,(Bad_At)
        ld bc,(Buf)
        ld a,l
        cp c
        jr nz,.prev
        ld a,h
        cp b
        jr z,.bits
.prev   dec de
        ld a,(de)
        cp (hl)
        ld hl,C_dup
        ld c,"D"
        jr z,Count
.bits   ld a,(Got)                              ; биты: какие не совпали - в общую маску
        ld hl,Exp
        xor (hl)
        ld hl,Bits_Or
        or (hl)
        ld (hl),a
        ld hl,C_bits
        ld c,"B"

; Счётчик HL +1; первая ошибка за всё время (вид C) - запомнить
Count   push bc
        call Inc32
        pop bc
        ld a,(F_kind)
        or a
        ret nz
        ld a,c
        ld (F_kind),a
        ld hl,(Bad_At)
        ld de,(Buf)
        or a
        sbc hl,de
        ld (F_at),hl
        ld a,(Exp)
        ld (F_exp),a
        ld a,(Got)
        ld (F_got),a
        ret

; Узор прохода: байт i = i + 7 * (i / 256) + Seed - соседние байты разные, сектора - тоже
Pat_Make
        ld hl,Pat
        ld bc,0
.p      ld a,b
        add a,a
        add a,a
        add a,a
        sub b                                   ; 7 * (i / 256)
        add a,c
        ld e,a
        ld a,(Seed)
        add a,e
        ld (hl),a
        inc hl
        inc bc
        ld a,b
        cp high N
        jr nz,.p
        ret

; Частота A: 0 - 3.5, 1 - 7, 2 - 14 МГц (SYSCONFIG, биты 1..0); на экран
Speed   ld (Spd),a
        ld bc,#20AF
        out (c),a
        ld de,13*256+0
        ld hl,t_cpu
        call print_at
        ld a,(Spd)
        add a,a
        add a,a                                 ; 4 знака на частоту
        ld e,a
        ld d,0
        ld hl,t_mhz
        add hl,de
        ld b,4
.m      ld a,(hl)
        call putc
        inc hl
        djnz .m
        ret

; (HL) - 32 бита +1
Inc32   inc (hl)
        ret nz
        inc hl
        inc (hl)
        ret nz
        inc hl
        inc (hl)
        ret nz
        inc hl
        inc (hl)
        ret

; ---- экран

Labels  ld hl,t_pass : ld de,2*256 : call print_at
        ld hl,t_write : ld de,4*256 : call print_at
        ld hl,t_skip : ld de,5*256 : call print_at
        ld hl,t_dup : ld de,6*256 : call print_at
        ld hl,t_bits : ld de,7*256 : call print_at
        ld hl,t_norep : ld de,8*256 : call print_at
        ld hl,t_first : ld de,10*256 : call print_at
        ld hl,t_key : ld de,21*256 : jp print_at

Show    ld hl,C_pass : ld de,2*256+10 : call Hex32_At
        ld hl,C_write : ld de,4*256+10 : call Hex32_At
        ld a,(Wbits_Or) : call phex
        ld hl,C_skip : ld de,5*256+10 : call Hex32_At
        ld hl,C_dup : ld de,6*256+10 : call Hex32_At
        ld hl,C_bits : ld de,7*256+10 : call Hex32_At
        ld a,(Bits_Or) : call phex
        ld hl,C_norep : ld de,8*256+10 : call Hex32_At
        ld a,(F_kind)                           ; первая ошибка: вид, номер байта в проходе, ждали, пришло
        or a
        ret z
        ld de,11*256
        call at
        ld a,(F_kind) : call putc
        ld hl,t_at : call print
        ld a,(F_at+1) : call phex
        ld a,(F_at) : call phex
        ld hl,t_exp : call print
        ld a,(F_exp) : call phex
        ld hl,t_got : call print
        ld a,(F_got) : jp phex

; 32 бита (HL) - 8 hex в строке D, колонке E; после - пробел
Hex32_At
        call at
        inc hl
        inc hl
        inc hl
        ld b,4
.h      ld a,(hl)
        push bc
        call phex
        pop bc
        dec hl
        djnz .h
        ld a," "
        jp putc

; ---- вывод текста: шрифт 8x8, cur_x / cur_y - знакоместо (как BUSTEST)
at      ld a,d : ld (cur_y),a
        ld a,e : ld (cur_x),a
        ret
print_at
        call at
print   ld a,(hl) : or a : ret z
        call putc : inc hl : jr print

putc    push hl : push de : push bc
        ld l,a : ld h,0 : add hl,hl : add hl,hl : add hl,hl
        ld de,font : add hl,de : ex de,hl
        ld a,(cur_y) : ld c,a
        and #18 : or #40 : ld h,a
        ld a,c : and 7 : rrca : rrca : rrca : ld l,a
        ld a,(cur_x) : add a,l : ld l,a
        ld b,8
.row    ld a,(de) : ld (hl),a : inc de : inc h : djnz .row
        ld hl,cur_x : inc (hl)
        pop bc : pop de : pop hl
        ret

phex    push af : rrca : rrca : rrca : rrca : call .dig : pop af
.dig    and #0F : add a,"0" : cp "9"+1 : jr c,.p : add a,7
.p      jp putc

        include "sdk/trinity_gs.asm"

; ---- тексты
t_head  db "TRINITY GS BUS TEST: 8 SECTORS",0
t_pass  db "PASSES",0
t_write db "WRITE",0
t_skip  db "SKIP",0
t_dup   db "DUP",0
t_bits  db "BITS",0
t_norep db "NO REPLY",0
t_first db "FIRST ERROR:",0
t_at    db " AT ",0
t_exp   db " EXP ",0
t_got   db " GOT ",0
t_cpu   db "CPU MHZ ",0
t_mhz   db "3.5 7   14  "
t_key   db "1 2 3 - CPU   SPACE - EXIT",0
t_done  db "STOPPED",0
t_nocard db "TRINITY GS NOT FOUND",0

; ---- переменные
cur_x   db 0
cur_y   db 0
Seed    db 0
Spd     db 0
Cnt_S   db 0
Sec     dw 0
Len     dw N                            ; сколько байт сверять (меньше N - чтение оборвалось)
Buf     dw 0
Bad_At  dw 0
Exp     db 0
Got     db 0
Bits_Or db 0
Wbits_Or db 0
F_kind  db 0                            ; первая ошибка: W / S / D / B (0 - не было)
F_at    dw 0
F_exp   db 0
F_got   db 0
C_pass  dd 0                            ; счётчики, 32 бита
C_write dd 0
C_skip  dd 0
C_dup   dd 0
C_bits  dd 0
C_norep dd 0

font    incbin "../../../FT2/srs/fonts/cp866bold.SpecCHR"

Pat     ds N                            ; узор прохода
Rbuf1   ds N                            ; первое чтение
Rbuf2   ds N                            ; второе
end
        ASSERT end < #C000

        savebin "gsbustest.bin",#8000,end-#8000
