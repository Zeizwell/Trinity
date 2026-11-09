; BUSTEST - проверка шины Спектрум <-> карта Trinity (TS-Config, запуск из WC: BUSTEST.SPG).
; Слот 512 байт с узором; дальше по кругу: SLOT_READ 512 и сверка каждого байта. Раз в REWRITE пакетов - узор
; заново (SLOT_WRITE): проверка и записи. Первая ошибка в пакете - по виду:
;   SKIP - байт пропал, хвост сдвинут (карта увидела лишнее чтение /RD);
;   DUP  - байт повторился (карта не успела положить следующий);
;   BITS - байт с другими битами (помеха на D0..D7), BITS OR - какие биты бывали плохими;
;   LEN  - ответ не той длины; NO REPLY - карта не ответила.
; Карта - только через SDK (sdk/trinity_ay.asm). Любая клавиша - выход (слот освободить, карту - по умолчанию).
; Порты TS-Config: VCONFIG #00AF, VPAGE #01AF, PAGE1 #11AF.

        DEVICE NOSLOT64K
        org #8000

N       equ 512                         ; байт в пакете чтения
REWRITE equ 64                          ; узор заново - раз в столько пакетов
SHOW    equ 8                           ; экран - раз в столько пакетов
WAIT_N  equ 20000                       ; опросов статуса до "нет ответа"

start   di
        ld sp,#7FFF
        ld bc,#00AF : xor a : out (c),a         ; видео: ZX 256x192
        ld bc,#01AF : ld a,5 : out (c),a        ; экран - страница 5
        ld bc,#11AF : ld a,5 : out (c),a        ; окно #4000 - страница 5
        xor a : out (#FE),a
        ld hl,#4000 : ld de,#4001 : ld bc,#17FF : ld (hl),0 : ldir
        ld hl,#5800 : ld de,#5801 : ld bc,#02FF : ld (hl),#07 : ldir

        ld hl,t_head : ld de,0 : call print_at
        call Pat_Make                           ; узор в буфер записи
        call Card_Find
        ld hl,t_nocard
        jp z,Stop
        call Slot_New
        ld hl,t_noslot
        jp z,Stop
        ld (Slot),a
        call Slot_Write
        call Labels

; ---- круг: чтение, сверка, раз в REWRITE - запись, экран, клавиша
Loop    call Slot_Read                          ; -> A: 0 ответ есть, 1 нет ответа
        or a
        jr nz,.norep
        call Check
        jr .cnt
.norep  ld hl,C_norep
        call Inc32
.cnt    ld hl,C_pack
        call Inc32
        ld a,(Cnt_W)
        inc a
        ld (Cnt_W),a
        cp REWRITE
        jr c,.show
        xor a
        ld (Cnt_W),a
        call Slot_Write
.show   ld a,(Cnt_S)
        inc a
        ld (Cnt_S),a
        cp SHOW
        jr c,.key
        xor a
        ld (Cnt_S),a
        call Show
.key    xor a                                   ; любая клавиша
        in a,(#FE)
        cpl
        and #1F
        jr z,Loop
        call Show
        ld hl,t_done

; Конец: текст HL внизу, слот - освободить, карту - по умолчанию
Stop    ld de,23*256+0
        call print_at
        ld a,(Slot)
        or a
        call nz,Slot_Free
        call Card
        call Ayfx_ModeAy
        call Wait
        cp AYFX_SVC_REPLY
        jr nz,.end
        call Card                               ; ответ MODE - дочитать
        ld hl,Rbuf
        call Ayfx_SvcReply
.end    jr .end

; ---- карта (SDK)

; Порт карты и чип 0 TurboSound: BC = #FFFD, #FF. Портит A, BC
Card    ld bc,AYFX_PORT_REG
        ld a,#FF
        out (c),a
        ret

; Ждать, пока карта занята -> A: AYFX_SVC_DONE / AYFX_SVC_REPLY, #FF - нет карты / не дождались
Wait    ld de,WAIT_N
.w      push de
        call Card
        call Ayfx_SvcState
        pop de
        cp AYFX_SVC_BUSY
        ret nz
        dec de
        ld a,d
        or e
        jr nz,.w
        ld a,#FF
        ret

; Карта есть? -> NZ - есть
Card_Find
        call Card
        call Ayfx_InfoSend
        call Wait
        cp AYFX_SVC_REPLY
        jr nz,.no
        call Card
        jp Ayfx_Detect
.no     xor a
        ret

; Пакет HL (DE байт) -> A: результат Wait; ответ - в Rbuf (DE - его байт)
Packet  push de
        push hl
        call Card
        pop hl
        pop de
        call Ayfx_SvcSend
        call Wait
        cp AYFX_SVC_REPLY
        ret nz
        call Card
        ld hl,Rbuf
        call Ayfx_SvcReply
        ld a,AYFX_SVC_REPLY
        ret

; SLOT_NEW 'TEST' N байт -> A = слот, Z - не дали
Slot_New
        ld hl,P_new
        ld de,P_NEW_N
        call Packet
        cp AYFX_SVC_REPLY
        jr nz,.no
        ld a,(Rbuf)
        or a
        ret
.no     xor a
        ret

; SLOT_WRITE: узор в слот (смещение 0, N байт)
Slot_Write
        ld a,(Slot)
        ld (Wbuf+3),a
        ld hl,Wbuf
        ld de,8+N
        jp Packet

; SLOT_READ: N байт со смещения 0 -> A: 0 ответ есть (Rbuf, длина - Rlen), 1 нет
Slot_Read
        ld a,(Slot)
        ld (P_read+3),a
        ld hl,P_read
        ld de,P_READ_N
        call Packet
        cp AYFX_SVC_REPLY
        ld a,1
        ret nz
        ld (Rlen),de
        xor a
        ret

; SLOT_FREE слот A
Slot_Free
        ld (P_free+3),a
        ld hl,P_free
        ld de,P_FREE_N
        jp Packet

; ---- узор и сверка

; Узор: байт i = i + 7 * (i / 256) - соседние байты разные, половины - тоже
Pat_Make
        ld hl,Wbuf
        ld (hl),AYFX_P_SLOT_WRITE               ; SLOT_WRITE: код, длина 5+N, слот, смещение 4, байты
        inc hl
        ld (hl),low (5+N)
        inc hl
        ld (hl),high (5+N)
        inc hl
        inc hl                                  ; слот - перед записью
        xor a
        ld (hl),a
        inc hl
        ld (hl),a
        inc hl
        ld (hl),a
        inc hl
        ld (hl),a
        inc hl
        ld de,Pat
        ld bc,0
.p      ld a,b
        add a,a
        add a,a
        add a,a
        sub b                                   ; 7 * (i / 256)
        add a,c
        ld (de),a
        ld (hl),a
        inc de
        inc hl
        inc bc
        ld a,b
        cp high N
        jr nz,.p
        ret

; Сверка Rbuf с узором: первая ошибка - по виду в счётчик, первая за всё время - в строку FIRST
Check   ld hl,(Rlen)
        ld de,N
        or a
        sbc hl,de
        jr z,.cmp
        ld hl,C_len
        jp Inc32
.cmp    ld hl,Rbuf
        ld de,Pat
        ld bc,N
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
.bad    ld (Bad_At),hl                          ; где: HL - в Rbuf, DE - в узоре
        ld a,(hl)
        ld (Got),a
        ld a,(de)
        ld (Exp),a
        inc de                                  ; следующий байт узора = пришедший - пропуск
        ld a,(de)
        cp (hl)
        ld hl,C_skip
        ld c,"S"
        jr z,.kind
        dec de                                  ; прошлый байт узора = пришедший - повтор (не на первом)
        ld hl,(Bad_At)
        ld a,l
        cp low Rbuf
        jr nz,.prev
        ld a,h
        cp high Rbuf
        jr z,.bits
.prev   dec de
        ld a,(de)
        cp (hl)
        ld hl,C_dup
        ld c,"D"
        jr z,.kind
.bits   ld a,(Got)                              ; биты: какие не совпали - в общую маску
        ld hl,Exp
        xor (hl)
        ld hl,Bits_Or
        or (hl)
        ld (hl),a
        ld hl,C_bits
        ld c,"B"
.kind   push bc
        call Inc32
        pop bc
        ld a,(F_kind)                           ; первая ошибка за всё время - запомнить
        or a
        ret nz
        ld a,c
        ld (F_kind),a
        ld hl,(Bad_At)
        ld de,Rbuf
        or a
        sbc hl,de
        ld (F_at),hl
        ld a,(Exp)
        ld (F_exp),a
        ld a,(Got)
        ld (F_got),a
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

Labels  ld hl,t_pack : ld de,2*256 : call print_at
        ld hl,t_skip : ld de,4*256 : call print_at
        ld hl,t_dup : ld de,5*256 : call print_at
        ld hl,t_bits : ld de,6*256 : call print_at
        ld hl,t_len : ld de,7*256 : call print_at
        ld hl,t_norep : ld de,8*256 : call print_at
        ld hl,t_first : ld de,10*256 : call print_at
        ld hl,t_key : ld de,21*256 : jp print_at

Show    ld hl,C_pack : ld de,2*256+10 : call Hex32_At
        ld hl,C_skip : ld de,4*256+10 : call Hex32_At
        ld hl,C_dup : ld de,5*256+10 : call Hex32_At
        ld hl,C_bits : ld de,6*256+10 : call Hex32_At
        ld a,(Bits_Or) : call phex
        ld hl,C_len : ld de,7*256+10 : call Hex32_At
        ld hl,C_norep : ld de,8*256+10 : call Hex32_At
        ld a,(F_kind)                           ; первая ошибка: вид, номер байта, ждали, пришло
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

; ---- вывод текста: шрифт 8x8, cur_x / cur_y - знакоместо (как aytest)
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

        include "sdk/trinity_ay.asm"

; ---- тексты
t_head  db "TRINITY BUS TEST: READ 512",0
t_pack  db "PACKETS",0
t_skip  db "SKIP",0
t_dup   db "DUP",0
t_bits  db "BITS",0
t_len   db "LEN",0
t_norep db "NO REPLY",0
t_first db "FIRST ERROR:",0
t_at    db " AT ",0
t_exp   db " EXP ",0
t_got   db " GOT ",0
t_key   db "ANY KEY - EXIT",0
t_done  db "STOPPED",0
t_nocard db "TRINITY CARD NOT FOUND",0
t_noslot db "NO SLOT ON THE CARD",0

; ---- пакеты: код, длина 2, данные
P_new   db AYFX_P_SLOT_NEW, 9,0, "TEST", low N, high N, 0,0, 0
P_NEW_N equ $-P_new
P_read  db AYFX_P_SLOT_READ, 7,0, 0, 0,0,0,0, low N, high N
P_READ_N equ $-P_read
P_free  db AYFX_P_SLOT_FREE, 1,0, 0
P_FREE_N equ $-P_free

; ---- переменные
cur_x   db 0
cur_y   db 0
Slot    db 0
Cnt_W   db 0
Cnt_S   db 0
Rlen    dw 0
Bad_At  dw 0
Exp     db 0
Got     db 0
Bits_Or db 0
F_kind  db 0                            ; первая ошибка: S / D / B (0 - не было)
F_at    dw 0
F_exp   db 0
F_got   db 0
C_pack  dd 0                            ; счётчики, 32 бита
C_skip  dd 0
C_dup   dd 0
C_bits  dd 0
C_len   dd 0
C_norep dd 0

font    incbin "../../../FT2/srs/fonts/cp866bold.SpecCHR"

Pat     ds N                            ; узор
Wbuf    ds 8+N                          ; пакет SLOT_WRITE
Rbuf    ds N+64                         ; ответ
end
        ASSERT end < #C000

        savebin "bustest.bin",#8000,end-#8000
