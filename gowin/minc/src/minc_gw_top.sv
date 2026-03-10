module minc_gw_top (
    input  logic        sys_clk,
    input  logic        sys_nrst,
    output logic [7:0]  pc_out,
    output logic [7:0]  port_a,
    output logic [7:0]  address,
    output logic        uart_tx,
    input  logic        uart_rx
);

//    logic [23:0] presc_cnt;
//    logic        int_clk;

    logic [7:0] sp_out;
    logic [7:0] data_in;

    logic [7:0] ram_data_out;
    logic [7:0] uartc_data_out;

    logic [7:0] data_out;
    logic       we;
    logic       wait_req;
    logic       wait_rel;
    wire        ram_ce = address > 8'h0F ? 1'b1 : 1'b0; // RAM is enabled for addresses > 0x0F

    logic [7:0] port_a_out;
    logic [7:0] port_a_in;
    logic [7:0] port_a_dir; // 1 = output, 0 = input
    assign port_a = port_a_out;

    assign data_in = ram_ce ? ram_data_out : uartc_data_out; // Mux data from RAM or UART based on address
    assign wait_req = address[7:4] == 5'b00001;

    minc u_minc (
        .clk    (sys_clk),
        .reset_n (sys_nrst),
        .pc_out(pc_out),
        .sp_out(sp_out),
        .address(address),
        .data_out(data_out),
        .we(we),
        .data_in(data_in),
        .wait_req(wait_req),
        .wait_rel(wait_rel)
    );

    Gowin_SP ram(
        .dout(ram_data_out), //output [7:0] dout
        .clk(~sys_clk), //input clk
        .ce(ram_ce), //input ce
        .oce(1'b1), //input oce
        .reset(~sys_nrst), //input reset
        .wre(we), //input wre
        .ad(address), //input [7:0] ad
        .din(data_out) //input [7:0] din
    );

    UART_MASTER_Top uartc(
		.I_CLK(sys_clk), //input I_CLK
		.I_RESETN(sys_nrst), //input I_RESETN
		.I_TX_EN(we & address[7:3] == 5'b00001), //input I_TX_EN
		.I_WADDR(address[2:0]), //input [2:0] I_WADDR
		.I_WDATA(data_out), //input [7:0] I_WDATA
		.I_RX_EN(address[7:3] == 5'b00001), //input I_RX_EN
		.I_RADDR(address[2:0]), //input [2:0] I_RADDR
		.O_RDATA(uartc_data_out), //output [7:0] O_RDATA
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

    always_ff @(negedge sys_clk or negedge sys_nrst) begin
        if (!sys_nrst) begin
            port_a_out <= 8'h00;
            port_a_dir <= 8'h00; // All inputs by default
            port_a_in <= 8'h00;
        end else begin
            // port_a_in <= port_a_out; // Loopback for testing
            if (address == 8'h00) begin 
                // data_in <= port_a_out; // Read from port A
                if (we) port_a_out <= data_out & port_a_dir; // Output only on bits set as output
            end else if (address == 8'h01) begin
                // data_in <= port_a_dir; // Read direction register
                if (we) port_a_dir <= data_out; // Set direction on address 0x01
            end else if (address == 8'h02) begin
                // data_in <= port_a_in & ~port_a_dir; // Read input values on bits set as input
            end
        end
    end
    
    always_ff @(negedge sys_clk or negedge sys_nrst) begin
        logic [3:0] int_cnt;
        if (!sys_nrst) begin
            int_cnt <= 4'd0;
            wait_rel <= 1'b0;
        end else if (wait_req) begin
            int_cnt <= int_cnt + 1'd1;
        end else if (int_cnt > 4'd2) begin
            int_cnt <= 4'd0;
            wait_rel <= 1'b1;
        end else if (wait_rel) begin
            wait_rel <= 1'b0;
        end
    end

//    always_ff @(posedge sys_clk or negedge sys_nrst) begin
//        if (!sys_nrst) begin
//            presc_cnt <= 24'd0;
//            int_clk   <= 1'b0;
//        end else if (presc_cnt == 24'd13_499) begin
//            presc_cnt <= 24'd0;
//            int_clk   <= ~int_clk;
//        end else begin
//            presc_cnt <= presc_cnt + 24'd1;
//        end
//    end

endmodule