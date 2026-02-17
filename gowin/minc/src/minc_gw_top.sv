module minc_gw_top (
    input  logic        sys_clk,
    input  logic        sys_nrst,
    output logic [7:0]  pc_out,
    output logic [7:0]  port_a,
    output logic [7:0]  address
);

//    logic [23:0] presc_cnt;
//    logic        int_clk;

    logic [7:0] sp_out;
    logic [7:0] data_in;
    logic [7:0] data_out;
    logic       we;
    wire        ram_ce = address > 8'h0F ? 1'b1 : 1'b0; // RAM is enabled for addresses > 0x0F

    logic [7:0] port_a_out;
    logic [7:0] port_a_in;
    logic [7:0] port_a_dir; // 1 = output, 0 = input
    assign port_a = port_a_out;

    minc u_minc (
        .clk    (sys_clk),
        .reset_n (sys_nrst),
        .pc_out(pc_out),
        .sp_out(sp_out),
        .address(address),
        .data_out(data_out),
        .we(we),
        .data_in(data_in)
    );

    Gowin_SP ram(
        .dout(data_in), //output [7:0] dout
        .clk(~sys_clk), //input clk
        .ce(ram_ce), //input ce
        .oce(1'b1), //input oce
        .reset(~sys_nrst), //input reset
        .wre(we), //input wre
        .ad(address), //input [7:0] ad
        .din(data_out) //input [7:0] din
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