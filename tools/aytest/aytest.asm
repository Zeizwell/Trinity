; AYTEST - проверка RP-AY на макете со стороны Спектрума (TS-Config, запуск из WC: AYTEST.SPG).
; 1) Каждый регистр 0..13: пишем через #BFFD 12 значений (00, FF, бегущая единица, 55, AA), читаем через #FFFD,
;    печатаем, какие БИТЫ не совпали (одни и те же биты везде - линия данных: провод / пайка).
; 2) Нота в канале A (~440 Гц): слышно - путь Спектрум -> RP -> чип работает.
; Сначала - итог теста чипа, что RP сделала при старте (#E0 - ошибок, #E1 - плохие биты; 00 00 - чип в порядке).
; Порты TS-Config: VCONFIG #00AF, VPAGE #01AF, PAGE1 #11AF.

        DEVICE NOSLOT64K
        org #8000

start:  di
        ld bc,#00AF : xor a : out (c),a        ; видео: ZX 256x192
        ld bc,#01AF : ld a,5 : out (c),a       ; экран - страница 5
        ld bc,#11AF : ld a,5 : out (c),a       ; окно #4000 - страница 5
        xor a : out (#FE),a                    ; бордюр чёрный
        ld hl,#4000 : ld de,#4001 : ld bc,#17FF : ld (hl),0 : ldir   ; экран - чисто
        ld hl,#5800 : ld de,#5801 : ld bc,#02FF : ld (hl),#07 : ldir ; атрибуты - белый по чёрному

        ld hl,t_head : call print              ; заголовок
        call newline : call newline

        ld hl,t_chip : call print              ; итог теста чипа при старте RP: #E0 - ошибок, #E1 - плохие биты
        ld bc,#FFFD : ld a,#E0 : out (c),a : in a,(c) : call phex
        ld hl,t_bits : call print
        ld bc,#FFFD : ld a,#E1 : out (c),a : in a,(c) : call phex
        call newline : call newline

        xor a : ld (reg),a
reg_loop:
        xor a : ld (bad),a : ld (have),a
        ld hl,pats : ld b,NPAT
pat_loop:
        push bc
        ld a,(reg) : ld bc,#FFFD : out (c),a   ; выбрать регистр
        ld a,(hl) : ld b,#BF : out (c),a       ; записать значение (#BFFD)
        ld b,#FF : in a,(c)                    ; прочитать (#FFFD)
        ld e,a                                 ; e - прочитали
        xor (hl)                               ; биты, что не совпали
        jr z,.ok
        ld d,a
        ld a,(bad) : or d : ld (bad),a         ; копим плохие биты
        ld a,(have) : or a : jr nz,.ok         ; первое несовпадение - запомнить
        inc a : ld (have),a
        ld a,(hl) : ld (fw),a
        ld a,e : ld (fr),a
.ok:    inc hl
        pop bc
        djnz pat_loop

        ld a,"R" : call putc                   ; строка: R03 OK / R03 BAD xx W xx R xx
        ld a,(reg) : call phex
        ld a," " : call putc
        ld a,(bad) : or a : jr nz,.bad
        ld hl,t_ok : call print
        jr .next
.bad:   ld hl,t_bad : call print
        ld a,(bad) : call phex
        ld hl,t_w : call print
        ld a,(fw) : call phex
        ld hl,t_r : call print
        ld a,(fr) : call phex
.next:  call newline
        ld a,(reg) : inc a : ld (reg),a
        cp 14 : jp c,reg_loop

        ld bc,#FFFD                            ; нота в канале A
        ld a,7 : out (c),a : ld b,#BF : ld a,#3E : out (c),a   ; R7: тон A
        ld b,#FF : xor a : out (c),a : ld b,#BF : ld a,#F8 : out (c),a  ; R0
        ld b,#FF : ld a,1 : out (c),a : ld b,#BF : xor a : out (c),a    ; R1
        ld b,#FF : ld a,8 : out (c),a : ld b,#BF : ld a,15 : out (c),a  ; R8: громкость 15
        call newline
        ld hl,t_tone : call print
        jr $

; ---- вывод текста: шрифт 8x8, cur_x / cur_y - знакоместо
putc:   push hl : push de : push bc
        ld l,a : ld h,0 : add hl,hl : add hl,hl : add hl,hl
        ld de,font : add hl,de : ex de,hl      ; de - символ
        ld a,(cur_y) : ld c,a
        and #18 : or #40 : ld h,a              ; треть экрана
        ld a,c : and 7 : rrca : rrca : rrca : ld l,a
        ld a,(cur_x) : add a,l : ld l,a
        ld b,8
.row:   ld a,(de) : ld (hl),a : inc de : inc h : djnz .row
        ld hl,cur_x : inc (hl)
        pop bc : pop de : pop hl
        ret

print:  ld a,(hl) : or a : ret z
        call putc : inc hl : jr print

newline: xor a : ld (cur_x),a
        ld hl,cur_y : inc (hl)
        ret

phex:   push af : rrca : rrca : rrca : rrca : call .dig : pop af
.dig:   and #0F : add a,"0" : cp "9"+1 : jr c,.p : add a,7
.p:     jp putc

t_head: db "RP-AY TEST: W #BFFD, R #FFFD",0
t_ok:   db "OK",0
t_bad:  db "BAD ",0
t_w:    db " W ",0
t_r:    db " R ",0
t_tone: db "TONE A ~440 HZ",0
t_chip: db "CHIP TEST: ERR ",0
t_bits: db " BAD BITS ",0

pats:   db #00,#FF,#01,#02,#04,#08,#10,#20,#40,#80,#55,#AA
NPAT    equ $-pats

cur_x:  db 0
cur_y:  db 0
reg:    db 0
bad:    db 0
have:   db 0
fw:     db 0
fr:     db 0

font:   incbin "../../../FT2/srs/fonts/cp866bold.SpecCHR"

        SAVEBIN "aytest.bin",start,$-start
