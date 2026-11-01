// Проверка CPLD: циклы Z80 на 14 МГц и модели RP. iverilog -o tb.vvp tb.v ../rtl/top.v && vvp tb.vvp
`timescale 1ns/1ps
module tb;
    localparam T = 71;                 // такт Z80 14 МГц
    reg clk = 0; always #15.6 clk = !clk;
    reg [15:0] a = 0;
    reg n_iorq=1, n_mreq=1, n_rd=1, n_wr=1, n_m1=1, n_rst=1;
    reg [7:0] z80_d; reg z80_oe = 0;
    wire [7:0] d, gs_d, ay_d; wire [5:0] gs_c; wire [1:0] ay_c;
    wire gs_wr, gs_rd, ay_wr, ay_rd, n_wait, n_iorqge, n_csromce_oe, ay_rst;
    reg gs_rdy = 0, ay_rdy = 0;
    assign d = z80_oe ? z80_d : 8'bz;
    pullup(n_wait);
    integer errors = 0;

    // Модели RP: на чтение кладут значение и поднимают RDY, запись запоминают
    reg gs_alive = 1, ay_alive = 1, gs_mask_oe = 0;
    reg [7:0] gs_ans = 0, ay_ans = 0, gs_mask = 0;
    reg [7:0] gs_got = 0, ay_got = 0; reg [5:0] gs_got_c = 0; reg [1:0] ay_got_c = 0;
    integer gs_wrs = 0, ay_wrs = 0, gs_rds = 0;
    assign gs_d = gs_mask_oe ? gs_mask : (gs_rd === 1'b0 && gs_alive) ? gs_ans : 8'bz;
    assign ay_d = (ay_rd === 1'b0 && ay_alive) ? ay_ans : 8'bz;
    always @(negedge gs_rd) if (gs_alive) begin gs_rds = gs_rds + 1; #100 gs_rdy = 1; end
    always @(posedge gs_rd) gs_rdy = 0;
    always @(negedge ay_rd) if (ay_alive) begin #100 ay_rdy = 1; end
    always @(posedge ay_rd) ay_rdy = 0;
    always @(posedge gs_wr) if ($time > 100) begin gs_got = gs_d; gs_got_c = gs_c; gs_wrs = gs_wrs + 1; end
    always @(posedge ay_wr) if ($time > 100) begin ay_got = ay_d; ay_got_c = ay_c; ay_wrs = ay_wrs + 1; end

    trinity_cpld u(.clk(clk), .a(a), .d(d), .n_iorq(n_iorq), .n_mreq(n_mreq), .n_rd(n_rd), .n_wr(n_wr),
        .n_m1(n_m1), .n_rst(n_rst), .n_dos(1'b1), .n_iodos(1'b1), .csrom(1'b0), .n_wait(n_wait),
        .n_iorqge(n_iorqge), .n_csromce_oe(n_csromce_oe), .gs_c(gs_c), .gs_d(gs_d), .gs_wr(gs_wr), .gs_rd(gs_rd),
        .gs_rdy(gs_rdy), .ay_c(ay_c), .ay_d(ay_d), .ay_wr(ay_wr), .ay_rd(ay_rd), .ay_rdy(ay_rdy), .ay_rst(ay_rst));

    task check(input integer ok, input [8*40-1:0] what);
        if (!ok) begin errors = errors + 1; $display("ERROR: %0s t=%0t", what, $time); end
    endtask

    // no_iorq = 1 - как Ева для AY: IORQ в слот не идёт
    task io_wr(input [15:0] port, input [7:0] v, input no_iorq);
    begin
        a = port; z80_d = v; #T;
        z80_oe = 1; if (!no_iorq) n_iorq = 0; n_wr = 0; #(2*T);
        check(n_wait === 1'b1, "WAIT on write");
        n_wr = 1; n_iorq = 1; #(T/2); z80_oe = 0; #(9*T);
    end endtask

    reg [7:0] rd_v;
    integer wait_seen;
    task io_rd(input [15:0] port, input no_iorq);
    begin
        a = port; #T;
        if (!no_iorq) n_iorq = 0; n_rd = 0;
        // Z80 смотрит WAIT на спаде TW: 1.5 такта от начала строба
        #(1.5*T); wait_seen = (n_wait === 1'b0);
        while (n_wait === 1'b0) #T;
        #T; rd_v = d;
        n_rd = 1; n_iorq = 1; #5;
        check(d === 8'hzz, "bus released at RD end");
        #(9*T);
    end endtask

    task load_mask(input [7:0] m);
    begin
        gs_mask = m; gs_mask_oe = 1; #40; gs_rdy = 1; #150; gs_rdy = 0; gs_mask_oe = 0; #100;
    end endtask

    initial begin
        #200;
        // Маска не загружена: порты не наши, шину не трогаем
        a = 16'h00B3; #10; check(n_iorqge === 1'b1, "IORQGE no mask");
        io_rd(16'h00B3, 0); check(gs_rds == 0, "read no mask");
        load_mask(8'h07);
        a = 16'h00B3; #10; check(n_iorqge === 1'b0, "IORQGE B3");
        a = 16'h1234; #10; check(n_iorqge === 1'b1, "IORQGE foreign");
        // Запись
        io_wr(16'h00B3, 8'hA5, 0);
        check(gs_wrs == 1 && gs_got == 8'hA5 && gs_got_c == 1, "write B3");
        io_wr(16'h00BB, 8'h5A, 0);
        check(gs_wrs == 2 && gs_got == 8'h5A && gs_got_c == 2, "write BB");
        io_wr(16'hFFFD, 8'h07, 1);
        check(ay_wrs == 1 && ay_got == 8'h07 && ay_got_c == 1, "write FFFD no IORQ");
        io_wr(16'hBFFD, 8'h3C, 1);
        check(ay_wrs == 2 && ay_got == 8'h3C && ay_got_c == 2, "write BFFD no IORQ");
        io_wr(16'hDFFD, 8'h11, 1);
        check(ay_wrs == 2, "DFFD is not AY");
        // Чтение: WAIT успел, ответ на шине, после цикла шина свободна
        gs_ans = 8'h77; io_rd(16'h00B3, 0);
        check(wait_seen && rd_v == 8'h77, "read B3");
        check(d === 8'hzz, "bus released");
        ay_ans = 8'h42; io_rd(16'hFFFD, 1);
        check(wait_seen && rd_v == 8'h42, "read FFFD");
        // RP молчит: WAIT отпущен, #FF
        gs_alive = 0; io_rd(16'h00BB, 0);
        check(rd_v == 8'hFF, "silent RP"); gs_alive = 1;
        // Память по адресу порта: не наша
        gs_rds = 0; ay_wrs = 0;
        a = 16'hFFFD; n_mreq = 0; #T; n_rd = 0; #(2*T); n_rd = 1; n_mreq = 1; #(9*T);
        a = 16'hFFFD; n_mreq = 0; #T; n_wr = 0; #(2*T); n_wr = 1; n_mreq = 1; #(9*T);
        check(ay_wrs == 0, "memory as AY");
        // IM2: M1 + IORQ без RD, IORQGE нет
        a = 16'h00B3; n_m1 = 0; #10; check(n_iorqge === 1'b1, "IORQGE on M1");
        n_iorq = 0; #(2*T); check(d === 8'hzz && gs_rds == 0, "IM2"); n_iorq = 1; n_m1 = 1; #(9*T);
        // RDY после чтения не должен перезаписать маску
        check(u.mask == 3'd7, "mask kept after reads");
        // Сброс
        n_rst = 0; #10; check(ay_rst === 1'b1, "AY reset"); n_rst = 1;
        if (errors == 0) $display("ALL OK"); else $display("ERRORS: %0d", errors);
        $finish;
    end
endmodule
