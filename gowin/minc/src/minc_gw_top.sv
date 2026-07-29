module minc_gw_top (
    input  logic        sys_clk,
    input  logic        sys_nrst,
    output logic [15:0]  pc_out,
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

//    logic [23:0] presc_cnt;
//    logic        int_clk;

// `define UART
`define PORTA
`define WAIT
`define I2C

    logic [15:0] sp_out;
    logic [7:0] data_in;

    logic [7:0] ram_data_out;
    logic [7:0] uart_data_out;
    logic [7:0] i2c_data_out;

    logic [7:0] data_out;
    logic       we;
    logic       wait_req;
    logic       wait_ma;
    logic       avma;
    logic [15:0] address;
    // logic       int_clk;
    assign address_out  = we ? data_out : data_in;
    assign address_out2 = ~address[5:0]; // For debugging: show address bits in reverse order
    wire        ram_ce  = address > 16'h00FF ? 1'b1 : 1'b0; // RAM is enabled for addresses > 0xFF
    assign wait_req_out = wait_req;
    assign avma_out = i2c_ce;
    assign we_out = we;

    logic [7:0] port_a_out;
    logic [7:0] port_a_in;
    logic [7:0] port_a_dir; // 1 = output, 0 = input
    assign port_a = port_a_out;

    logic [3:0] int_cnt;

    assign data_in =
    `ifdef UART
                        uart_ce ? uart_data_out :
    `endif
    `ifdef I2C
                        i2c_ce ? i2c_data_out :
    `endif
                        ram_ce ? ram_data_out : 8'hxx; // Mux data from RAM or UART based on address

    minc u_minc (
        .clk    (sys_clk),
        .reset_n (sys_nrst),
        .pc_out(pc_out),
        .sp_out(sp_out),
        .address(address),
        .data_out(data_out),
        .we(we),
        .avma(avma),
        .data_in(data_in),
        .wait_req(wait_req)
    );

    Gowin_SP ram(
        .dout(ram_data_out), //output [7:0] dout
        .clk(sys_clk), //input clk
        .ce(ram_ce), //input ce
        .oce(1'b1), //input oce
        .reset(~sys_nrst), //input reset
        .wre(we), //input wre
        .ad(address), //input [11:0] ad
        .din(data_out) //input [7:0] din
    );

`ifdef UART
    localparam UART_ADDRESS_BASE = 16'h0008;
    localparam UART_ADDRESS_LEN  = 3; // 2^3 = 8 bytes
    wire uart_ce = (address[15:UART_ADDRESS_LEN] == UART_ADDRESS_BASE[15:UART_ADDRESS_LEN]) ? 1'b1 : 1'b0;
    UART_MASTER_Top uartc(
        .I_CLK(sys_clk), //input I_CLK
        .I_RESETN(sys_nrst), //input I_RESETN
        .I_TX_EN(we && uart_ce && wait_ma), //input I_TX_EN
        .I_WADDR(address[2:0]), //input [2:0] I_WADDR
        .I_WDATA(data_out), //input [7:0] I_WDATA
        .I_RX_EN(uart_ce && wait_ma), //input I_RX_EN
        .I_RADDR(address[2:0]), //input [2:0] I_RADDR
        .O_RDATA(uart_data_out), //output [7:0] O_RDATA
        .SIN(uart_rx), //input SIN
        .RxRDYn(), //output RxRDYn
        .SOUT(uart_tx), //output SOUT
        .TxRDYn(), //output TxRDYn
        .DDIS(), //output DDIS
        .INTR(), //output INTR
        .DCDn(1'b1), //input DCDn
        .CTSn(1'b1), //input CTSn
        .DSRn(1'b1), //input DSRn
        .RIn(1'b1), //input RIn
        .DTRn(), //output DTRn
        .RTSn() //output RTSn
    );
`ifndef WAIT
`define WAIT
`endif
`endif

`ifdef PORTA
localparam PORT_A_BASE = 16'h0004;
    always_ff @(posedge sys_clk or negedge sys_nrst) begin
        if (!sys_nrst) begin
            port_a_out <= 8'h00;
            port_a_dir <= 8'h00; // All inputs by default
            port_a_in <= 8'h00;
        end else begin
            // port_a_in <= port_a_out; // Loopback for testing
            if (address == 16'h0004) begin 
                // data_in <= port_a_out; // Read from port A
                if (we) port_a_out <= data_out & port_a_dir; // Output only on bits set as output
            end else if (address == 16'h0005) begin
                // data_in <= port_a_dir; // Read direction register
                if (we) port_a_dir <= data_out; // Set direction on address 0x01
            end else if (address == 16'h0006) begin
                // data_in <= port_a_in & ~port_a_dir; // Read input values on bits set as input
            end
        end
    end
`endif

`ifdef I2C
    localparam I2C_ADDRESS_BASE = 16'h0010;
    localparam I2C_ADDRESS_LEN  = 3; // 2^3 = 8 bytes
    wire i2c_ce = (address[15:I2C_ADDRESS_LEN] == I2C_ADDRESS_BASE[15:I2C_ADDRESS_LEN]) ? 1'b1 : 1'b0;
    I2C_MASTER_Top i2c(
		.I_CLK(sys_clk), //input I_CLK
		.I_RESETN(sys_nrst), //input I_RESETN
		.I_TX_EN(i2c_ce && we && wait_ma), //input I_TX_EN
		.I_WADDR(address[2:0]), //input [2:0] I_WADDR
		.I_WDATA(data_out), //input [7:0] I_WDATA
		.I_RX_EN(i2c_ce && wait_ma), //input I_RX_EN
		.I_RADDR(address[2:0]), //input [2:0] I_RADDR
		.O_RDATA(i2c_data_out), //output [7:0] O_RDATA
		.O_IIC_INT(), //output O_IIC_INT
		.SCL(i2c_scl), //inout SCL
		.SDA(i2c_sda)  //inout SDA
	);
`endif

`ifdef WAIT
    // assign wait_req = 8'h10 > address ? 1'b1 : 1'b0;
    parameter WAITp4 = 8;
    logic [WAITp4:0] wait_sr;
    assign wait_req = (wait_sr[WAITp4-2:1] != 'b0) ? 1'b1 : 1'b0;
    assign wait_ma = wait_sr[1];
    always_ff @(posedge sys_clk or negedge sys_nrst) begin
        if (!sys_nrst) begin
            wait_sr <= 'b1;
        end else begin
            if (((address[15:3] == 13'b00001) || (address[15:4] == 11'h001)) && avma) begin
                {wait_sr[0], wait_sr[WAITp4:1]} <= wait_sr;
            end else
                wait_sr <= 'b1;
        end
    end
`endif

`ifndef WAIT
    assign wait_req = 1'b0;
`endif 

    // always_ff @(posedge sys_clk or negedge sys_nrst) begin
    //     logic [23:0] presc_cnt;
    //     if (!sys_nrst) begin
    //         presc_cnt <= 24'd0;
    //         int_clk   <= 1'b0;
    //     end else if (presc_cnt == 24'd13_499) begin
    //         presc_cnt <= 24'd0;
    //         int_clk   <= ~int_clk;
    //     end else begin
    //         presc_cnt <= presc_cnt + 24'd1;
    //     end
    // end

endmodule