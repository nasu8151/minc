// minc-16 SoC top level (GW1NR-9C).
//
// gowin/minc/src/minc_gw_top.sv (minc-8 版) の 16bit 移植。違いは3点:
//   1. データバスが 16bit、`we` がバイトレーン2本 (we[0]=偶数バイト, we[1]=奇数バイト)
//   2. データRAMが Gowin_SP ×2 (偶数レーン / 奇数レーン)。ワード index = address[12:1]
//   3. SP のMMIO (`0x0000`/`0x0001`) が無い — minc-16 の SP は r15 そのもの
//
// 周辺IPは全て8bitレジスタなので、アクセスは `ldb`/`stb` (バイト幅) 前提。
// 読みは同じバイトを両レーンに複製して返す (CPU側が address[0] でレーンを選ぶ)。
// 書きは CPU が st_data をあらかじめ両レーンに複製しているので data_out[7:0] を見ればよい。
//
// データ空間メモリマップ (バイトアドレス):
//   0x0000-0x0001  未使用 (minc-8 の SP MMIO 跡地)
//   0x0002-0x0003  PSR / PSR_SHADOW ... CPU内部で応答するのでここでは触らない
//   0x0004-0x0006  PORT A (out / dir / in)
//   0x0008-0x000F  UART
//   0x0010-0x0017  I2C
//   0x0018-0x001F  TIMER8
//   0x0100-0x1FFF  データRAM (8KB)
// 下位256バイトをMMIOに空けてあるのは、絶対アドレスモード (`stw n,rs` / `ldw rd,n`)
// の abs8 が 0x00-0xFF しか届かないため。
module minc_16_gw_top (
    input  logic        sys_clk,
    input  logic        sys_nrst,
    output logic [15:0] pc_out,
    output logic [7:0]  port_a,
    output logic [7:0]  address_out,
    output logic [5:0]  address_out2,
    output logic        uart_tx,
    input  logic        uart_rx,
    inout  logic        i2c_scl,
    inout  logic        i2c_sda,
    output logic        wait_req_out,
    output logic        avma_out,
    output logic        we_out
);

// `define UART
// `define PORTA
// `define WAIT
// `define TIMER8
// I2C は src/i2c_master/ をこのプロジェクトへコピーしてから有効化すること
// `define I2C

// ウェイト無しだと周辺IPのリードストローブが2サイクル伸びる (S_MA + S_WB) ため、
// 周辺IPを載せる構成では WAIT を強制する。
`ifdef UART
  `ifndef WAIT
    `define WAIT
  `endif
`endif
`ifdef I2C
  `ifndef WAIT
    `define WAIT
  `endif
`endif
`ifdef TIMER8
  `ifndef WAIT
    `define WAIT
  `endif
`endif

    logic [15:0] sp_out;
    logic [15:0] address;
    logic [15:0] data_in;
    logic [15:0] data_out;
    logic [1:0]  we;
    logic        avma;
    logic        wait_req;
    logic        bus_stb;

    logic [15:0] ram_data_out;
    logic [7:0]  uart_data_out;
    logic [7:0]  i2c_data_out;
    logic [7:0]  timer8_data_out;

    logic [3:0]  irq_line;

    // read mux 用の1サイクル遅れの選択信号 (理由は「Read data mux」節を参照)
    logic ram_sel, porta_sel, porta_hi, uart_sel, i2c_sel, timer8_sel;

    // RAM は 0x0100 以上。0x2000 以上はエイリアスになる (address[12:1] しか見ない)
    wire ram_ce = (address[15:8] != 8'h00);

    assign address_out  = (|we) ? data_out[7:0] : data_in[7:0];
    assign address_out2 = ~address[5:0]; // For debugging: show address bits in reverse order
    assign wait_req_out = wait_req;
    assign avma_out     = avma;
    assign we_out       = |we;

    /* ------------------------------------------------------------------ *
     * Address decode
     * ------------------------------------------------------------------ */
    // read mux より前にまとめておく (Gowin Synthesis は前方参照を通すが
    // iverilog での lint が通らなくなるため)。
`ifdef PORTA
    localparam PORT_A_BASE = 16'h0004;
    // 0x0004/0x0005 = ワード2の下位/上位レーン、0x0006 = ワード3の下位レーン。
    // porta_ce はライブ (書き込み用)、読み出しは下の porta_sel / porta_hi 側を使う。
    wire porta_ce = (address[15:2] == PORT_A_BASE[15:2]);
    logic [7:0] port_a_out;
    logic [7:0] port_a_dir; // 1 = output, 0 = input
    wire  [7:0] port_a_in = 8'h00; // TODO: port_a を inout パッドにするまで入力は常に0
    wire [15:0] porta_word = porta_hi ? {8'h00, port_a_in & ~port_a_dir}
                                      : {port_a_dir, port_a_out};
`endif
`ifdef UART
    localparam UART_ADDRESS_BASE = 16'h0008;
    localparam UART_ADDRESS_LEN  = 3; // 2^3 = 8 bytes
    wire uart_ce = (address[15:UART_ADDRESS_LEN] == UART_ADDRESS_BASE[15:UART_ADDRESS_LEN]);
`endif
`ifdef I2C
    localparam I2C_ADDRESS_BASE = 16'h0010;
    localparam I2C_ADDRESS_LEN  = 3; // 2^3 = 8 bytes
    wire i2c_ce = (address[15:I2C_ADDRESS_LEN] == I2C_ADDRESS_BASE[15:I2C_ADDRESS_LEN]);
`endif
`ifdef TIMER8
    localparam TIMER8_ADDRESS_BASE = 16'h0018;
    localparam TIMER8_ADDRESS_LEN  = 3; // 2^3 = 8 bytes
    wire timer8_ce = (address[15:TIMER8_ADDRESS_LEN] == TIMER8_ADDRESS_BASE[15:TIMER8_ADDRESS_LEN]);
`endif

    // ウェイトを入れる相手 = Gowin IP コアだけ。RAM / PSR(0x0002-0x0003) / PORT A は
    // 1サイクルで応答する素のレジスタなので伸ばさない (下の wait states 節を参照)。
    wire slow_ce =
    `ifdef UART
                   uart_ce   ||
    `endif
    `ifdef I2C
                   i2c_ce    ||
    `endif
    `ifdef TIMER8
                   timer8_ce ||
    `endif
                   1'b0;

    /* ------------------------------------------------------------------ *
     * Read data mux
     * ------------------------------------------------------------------ */
    // **選択信号はライブのデコードではなく1サイクル遅らせたものを使うこと。**
    // `pop`/`ret`/`reti` は S_MA で「古いSP」をアドレスに出して RAM を読み、
    // S_MA の終わりに SP を更新するので S_WB ではもう「新しいSP」が出ている。
    // RAM の dout は S_MA→S_WB のエッジで確定して保持されるのに対し、選択信号を
    // ライブのアドレスから作ると S_WB で外れて x を掴み、`ret`/`reti` の戻り先 PC が
    // 不定になる。RAM の出力レジスタと歩調を合わせて選択信号も1段遅らせる。
    // (minc16_tb.sv は RAM 直結でマルチプレクサが無いためこの問題が出ない)
    always_ff @(posedge sys_clk or negedge sys_nrst) begin
        if (!sys_nrst) begin
            {ram_sel, porta_sel, porta_hi, uart_sel, i2c_sel, timer8_sel} <= 6'b0;
        end else begin
            ram_sel    <= ram_ce;
            porta_hi   <= address[1];
    `ifdef PORTA
            porta_sel  <= porta_ce;
    `endif
    `ifdef UART
            uart_sel   <= uart_ce;
    `endif
    `ifdef I2C
            i2c_sel    <= i2c_ce;
    `endif
    `ifdef TIMER8
            timer8_sel <= timer8_ce;
    `endif
        end
    end

    // 周辺IPは8bitなので同じバイトを両レーンへ複製する。PSR (0x0002/0x0003) は
    // CPU が data_in を無視して自前の値を使うのでここには出てこない。
    assign data_in =
    `ifdef PORTA
                        porta_sel  ? porta_word :
    `endif
    `ifdef UART
                        uart_sel   ? {uart_data_out,   uart_data_out}   :
    `endif
    `ifdef I2C
                        i2c_sel    ? {i2c_data_out,    i2c_data_out}    :
    `endif
    `ifdef TIMER8
                        timer8_sel ? {timer8_data_out, timer8_data_out} :
    `endif
                        ram_sel    ? ram_data_out : 16'hxxxx;

    minc16 u_minc16 (
        .clk     (sys_clk),
        .reset_n (sys_nrst),
        .pc_out  (pc_out),
        .sp_out  (sp_out),
        .address (address),
        .data_out(data_out),
        .we      (we),
        .avma    (avma),
        .data_in (data_in),
        .wait_req(wait_req),
        .irq_in  (irq_line)
    );

    /* ------------------------------------------------------------------ *
     * Data RAM: 4096word x 16bit = 8KB
     * ------------------------------------------------------------------ */
    // Gowin_SP は 4096x8。バイトレーンごとに1個ずつ並べて wre を独立させる
    // (専用のバイトイネーブルプリミティブは要らない — Hardware.md 設計メモ参照)。
    Gowin_SP ram_even (
        .dout (ram_data_out[7:0]),  //output [7:0] dout
        .clk  (sys_clk),            //input clk
        .oce  (1'b1),               //input oce
        .ce   (ram_ce),             //input ce
        .reset(~sys_nrst),          //input reset
        .wre  (we[0]),              //input wre
        .ad   (address[12:1]),      //input [11:0] ad
        .din  (data_out[7:0])       //input [7:0] din
    );

    Gowin_SP ram_odd (
        .dout (ram_data_out[15:8]), //output [7:0] dout
        .clk  (sys_clk),            //input clk
        .oce  (1'b1),               //input oce
        .ce   (ram_ce),             //input ce
        .reset(~sys_nrst),          //input reset
        .wre  (we[1]),              //input wre
        .ad   (address[12:1]),      //input [11:0] ad
        .din  (data_out[15:8])      //input [7:0] din
    );

    /* ------------------------------------------------------------------ *
     * PORT A
     * ------------------------------------------------------------------ */
`ifdef PORTA
    assign port_a = port_a_out;

    // 書き込みを bus_stb でゲートするのは必須。CPU の data_out が有効なのは S_WB の
    // 最初の1サイクルだけ (その後 16'hxxxx に戻る) なのに対し、`we` はウェイトで
    // 延びた S_WB の間ずっと立っている。ゲートしないと2サイクル目以降の x で
    // レジスタが壊れる。
    always_ff @(posedge sys_clk or negedge sys_nrst) begin
        if (!sys_nrst) begin
            port_a_out <= 8'h00;
            port_a_dir <= 8'h00; // All inputs by default
        end else if (porta_ce && bus_stb && !address[1]) begin
            if (we[0]) port_a_out <= data_out[7:0]  & port_a_dir; // 0x0004
            if (we[1]) port_a_dir <= data_out[15:8];              // 0x0005
        end
    end
`else
    assign port_a = 8'h00;
`endif

    /* ------------------------------------------------------------------ *
     * UART
     * ------------------------------------------------------------------ */
`ifdef UART
    UART_MASTER_Top uartc(
        .I_CLK   (sys_clk),                          //input I_CLK
        .I_RESETN(sys_nrst),                         //input I_RESETN
        .I_TX_EN ((|we) && uart_ce && bus_stb),      //input I_TX_EN
        .I_WADDR (address[2:0]),                     //input [2:0] I_WADDR
        .I_WDATA (data_out[7:0]),                    //input [7:0] I_WDATA
        .I_RX_EN (uart_ce && bus_stb),               //input I_RX_EN
        .I_RADDR (address[2:0]),                     //input [2:0] I_RADDR
        .O_RDATA (uart_data_out),                    //output [7:0] O_RDATA
        .SIN     (uart_rx),                          //input SIN
        .RxRDYn  (),                                 //output RxRDYn
        .SOUT    (uart_tx),                          //output SOUT
        .TxRDYn  (),                                 //output TxRDYn
        .DDIS    (),                                 //output DDIS
        .INTR    (),                                 //output INTR -- irq_line[1] へ繋ぐならここ
        .DCDn    (1'b1),                             //input DCDn
        .CTSn    (1'b1),                             //input CTSn
        .DSRn    (1'b1),                             //input DSRn
        .RIn     (1'b1),                             //input RIn
        .DTRn    (),                                 //output DTRn
        .RTSn    ()                                  //output RTSn
    );
`else
    assign uart_tx = 1'b1;
`endif

    /* ------------------------------------------------------------------ *
     * I2C
     * ------------------------------------------------------------------ */
`ifdef I2C
    I2C_MASTER_Top i2c(
        .I_CLK    (sys_clk),                         //input I_CLK
        .I_RESETN (sys_nrst),                        //input I_RESETN
        .I_TX_EN  (i2c_ce && (|we) && bus_stb),      //input I_TX_EN
        .I_WADDR  (address[2:0]),                    //input [2:0] I_WADDR
        .I_WDATA  (data_out[7:0]),                   //input [7:0] I_WDATA
        .I_RX_EN  (i2c_ce && bus_stb),               //input I_RX_EN
        .I_RADDR  (address[2:0]),                    //input [2:0] I_RADDR
        .O_RDATA  (i2c_data_out),                    //output [7:0] O_RDATA
        .O_IIC_INT(),                                //output O_IIC_INT
        .SCL      (i2c_scl),                         //inout SCL
        .SDA      (i2c_sda)                          //inout SDA
    );
`else
    assign i2c_scl = 1'bz;
    assign i2c_sda = 1'bz;
`endif

    /* ------------------------------------------------------------------ *
     * TIMER8
     * ------------------------------------------------------------------ */
    wire timer8_ovf_int;
`ifdef TIMER8
    TIMER8 timer8 (
        .I_CLK     (sys_clk),
        .I_RESETN  (sys_nrst),
        .I_TX_EN   (timer8_ce && (|we) && bus_stb),
        .I_WADDR   (address[2:0]),
        .I_WDATA   (data_out[7:0]),
        .I_RX_EN   (timer8_ce && bus_stb),
        .I_RADDR   (address[2:0]),
        .O_RDATA   (timer8_data_out),
        .O_OVERFLOW(),
        .O_COMPARE (),
        .O_OVF_INT (timer8_ovf_int),
        .O_CMP_INT ()
    );
`else
    assign timer8_ovf_int = 1'b0;
`endif

    // 割り込みはレベルトリガ・固定優先度。irq_line[0] が最優先。
    assign irq_line = {3'b000, timer8_ovf_int};

    /* ------------------------------------------------------------------ *
     * Wait states
     * ------------------------------------------------------------------ */
    // 引き伸ばすのは `slow_ce` (= Gowin IP コア) へのアクセスだけ。**PSR を含む
    // 低位アドレスを伸ばしてはいけない**: CPU 内部の PSR MMIO 書き込みは S_WB の
    // 毎サイクル実行されるのに data_out が有効なのは最初の1サイクルだけなので、
    // 伸ばすと2サイクル目以降の x が PSR に入り IE が不定になる (= `sei` 相当の
    // `stb 2,rN` の直後に暴走する)。同じ理由で PORT A も伸ばさない。
`ifdef WAIT
    parameter WAITp4 = 8;
    logic [WAITp4:0] wait_sr;
    assign wait_req = (wait_sr[WAITp4-2:1] != 'b0) ? 1'b1 : 1'b0;
    always_ff @(posedge sys_clk or negedge sys_nrst) begin
        if (!sys_nrst) begin
            wait_sr <= 'b1;
        end else begin
            if (slow_ce && avma) begin
                {wait_sr[0], wait_sr[WAITp4:1]} <= wait_sr;
            end else
                wait_sr <= 'b1;
        end
    end
    // bus_stb: S_WB のうち CPU の data_out が有効な最初の1サイクルだけ 1。
    // ウェイトを掛けない相手なら S_WB 自体が1サイクルなので常時 1 でよい。
    assign bus_stb = slow_ce ? wait_sr[1] : 1'b1;
`else
    assign wait_req = 1'b0;
    assign bus_stb  = 1'b1;
`endif

endmodule
