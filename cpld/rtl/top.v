// CPLD карты Trinity (EPM3128ATC100-10N). Шпаргалка и решения - doc/cpld.md.
// Основа - MultiSound (детектор цикла, IORQGE, защёлки) и NeoGS (порты GS, IORQGE по адресу).
module trinity_cpld(
    input            clk,          // такт от RP-GS, 32 МГц
    // шина Спектрума
    input     [15:0] a,
    inout      [7:0] d,
    input            n_iorq, n_mreq, n_rd, n_wr, n_m1, n_rst,
    input            n_dos, n_iodos, csrom,    // заведены на будущее, пока не нужны
    output           n_wait,
    output           n_iorqge,     // 0 - буфер выдаёт 5 В на IORQGE
    output           n_csromce_oe, // окно ПЗУ не трогаем
    // RP-GS
    output reg [5:0] gs_c,         // код порта, 0 - не нам
    inout      [7:0] gs_d,
    output reg       gs_wr, gs_rd, // стробы, активный 0
    input            gs_rdy,       // 1 - ответ лежит на gs_d
    // RP-AY
    output reg [1:0] ay_c,
    inout      [7:0] ay_d,
    output reg       ay_wr, ay_rd,
    input            ay_rdy,
    output           ay_rst        // 1 - сброс
);

// Коды портов на ножках RP
localparam [5:0] CODE_NONE = 6'd0, CODE_B3 = 6'd1, CODE_BB = 6'd2, CODE_33 = 6'd3,
                 CODE_C4 = 6'd4, CODE_C5 = 6'd5, CODE_C6 = 6'd6, CODE_C7 = 6'd7;
localparam [1:0] CODE_AY_NONE = 2'd0, CODE_FFFD = 2'd1, CODE_BFFD = 2'd2;

// Биты маски устройств (п.38), после включения всё выключено
localparam MASK_GS = 0, MASK_OPL = 1, MASK_AY = 2;

// Времена в тактах clk (31 нс)
localparam [6:0] WAIT_MAX = 7'd64;   // RP молчит 2 мкс: отпустить WAIT, ответ #FF
localparam [2:0] WR_PULSE = 3'd4;    // строб записи в RP, 125 нс

localparam [2:0] S_IDLE = 3'd0, S_READ = 3'd1, S_HOLD = 3'd2, S_WRITE = 3'd3,
                 S_SETUP = 3'd4, S_PULSE = 3'd5, S_AFTER = 3'd6;

reg  [2:0] mask;
reg  [2:0] state;
reg  [6:0] timer;
reg  [7:0] latch;    // данные: запись с шины в RP или ответ RP на шину
reg        to_ay;    // текущий цикл - RP-AY
reg        wait_on;

// После включения регистры MAX3000A в нуле, симулятору - то же самое
initial begin mask = 0; state = S_IDLE; wait_on = 0; end

// Дешифрация адреса
wire [5:0] code_opl =
    !mask[MASK_OPL] ? CODE_NONE :
    a[7:0] == 8'hC4 ? CODE_C4 :
    a[7:0] == 8'hC5 ? CODE_C5 :
    a[7:0] == 8'hC6 ? CODE_C6 :
    a[7:0] == 8'hC7 ? CODE_C7 :
    CODE_NONE;
wire [5:0] code_gs =
    !mask[MASK_GS]  ? code_opl :
    a[7:0] == 8'hB3 ? CODE_B3 :
    a[7:0] == 8'hBB ? CODE_BB :
    a[7:0] == 8'h33 ? CODE_33 :
    code_opl;
wire [1:0] code_ay =
    !mask[MASK_AY]                          ? CODE_AY_NONE :
    a[15:13] == 3'b111 && a[3:0] == 4'b1101 ? CODE_FFFD :
    a[15:14] == 2'b10  && a[3:0] == 4'b1101 ? CODE_BFFD :
    CODE_AY_NONE;

// Цикл ввода-вывода по такту, как MultiSound, но по фронту: от спада до фронта логика не успевает (18.5 нс при 15.6).
// GS и OPL - честный IORQ. AY: Ева не пускает IORQ своих портов в слот, поэтому RD / WR без MREQ и M1.
// Цикл IM2 (M1 + IORQ без RD) не попадает ни туда, ни туда.
reg io_iorq, io_nomreq;
always @(posedge clk) begin
    io_iorq   <= n_m1 && !n_iorq && (!n_rd || !n_wr);
    io_nomreq <= n_m1 && n_mreq  && (!n_rd || !n_wr);
end

wire hit_gs = io_iorq   && code_gs != CODE_NONE;
wire hit_ay = io_nomreq && code_ay != CODE_AY_NONE;
wire hit    = hit_gs || hit_ay;

// Ответ RP - через два триггера
reg [1:0] gs_rdy_s, ay_rdy_s;
always @(posedge clk) begin
    gs_rdy_s <= {gs_rdy_s[0], gs_rdy};
    ay_rdy_s <= {ay_rdy_s[0], ay_rdy};
end
wire rdy = to_ay ? ay_rdy_s[1] : gs_rdy_s[1];

always @(posedge clk) begin
    case (state)
    S_IDLE: begin
        gs_wr <= 1; gs_rd <= 1; ay_wr <= 1; ay_rd <= 1;   // после включения - в покой
        gs_c  <= CODE_NONE; ay_c <= CODE_AY_NONE;
        wait_on <= 0;
        // RDY без чтения - RP-GS загружает маску
        if (gs_rdy_s[1]) mask <= gs_d[2:0];
        else if (hit) begin
            to_ay <= !hit_gs;
            gs_c  <= hit_gs ? code_gs : CODE_NONE;
            ay_c  <= hit_gs ? CODE_AY_NONE : code_ay;
            timer <= 0;
            if (!n_rd) begin
                wait_on <= 1;
                if (hit_gs) gs_rd <= 0; else ay_rd <= 0;
                state <= S_READ;
            end else
                state <= S_WRITE;
        end
    end
    // Чтение: держим WAIT, пока RP не положит ответ
    S_READ: begin
        timer <= timer + 1'b1;
        if (rdy) begin
            latch   <= to_ay ? ay_d : gs_d;
            wait_on <= 0;
            state   <= S_HOLD;
        end else if (timer == WAIT_MAX) begin
            latch   <= 8'hFF;
            wait_on <= 0;
            state   <= S_HOLD;
        end
    end
    // Ответ на шине до конца цикла
    S_HOLD:
        if (!hit) begin
            gs_rd <= 1; ay_rd <= 1;
            state <= S_IDLE;
        end
    // Запись: последний снимок, пока WR ещё внизу, - данные точно стоят (задний фронт, как NeoGS)
    S_WRITE:
        if (!n_wr) latch <= d;
        else       state <= S_SETUP;
    // Данные и код выставлены раньше строба
    S_SETUP: begin
        timer <= 0;
        if (to_ay) ay_wr <= 0; else gs_wr <= 0;
        state <= S_PULSE;
    end
    S_PULSE: begin
        timer <= timer + 1'b1;
        if (timer[2:0] == WR_PULSE - 1'b1) begin
            gs_wr <= 1; ay_wr <= 1;
            state <= S_AFTER;
        end
    end
    // Данные держим такт после строба
    S_AFTER:
        state <= S_IDLE;
    default:
        state <= S_IDLE;
    endcase
end

wire rp_out = state == S_SETUP || state == S_PULSE || state == S_AFTER;

assign d    = state == S_HOLD && !n_rd ? latch : 8'bz;
assign gs_d = rp_out && !to_ay ? latch : 8'bz;
assign ay_d = rp_out &&  to_ay ? latch : 8'bz;

assign n_wait = wait_on ? 1'b0 : 1'bz;
// IORQGE - только по адресу и M1 = 1, без IORQ: машина успевает отключить своё до IORQ
assign n_iorqge = !(n_m1 && (code_gs != CODE_NONE || code_ay != CODE_AY_NONE));
assign n_csromce_oe = 1'b1;
assign ay_rst = !n_rst;

endmodule
