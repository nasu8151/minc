module minc_gw_top (
    input  logic        sys_clk,
    input  logic        sys_nrst,
    output logic [7:0]  pc_out,
    output logic [7:0]  top_out,
    output logic [7:0]  address
);

//    logic [23:0] presc_cnt;
//    logic        int_clk;

    logic [7:0] sp_out;
    logic [7:0] data_in;
    logic [7:0] data_out;
    logic       we;

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
        .ce(1'b1), //input ce
        .oce(1'b1), //input oce
        .reset(~sys_nrst), //input reset
        .wre(we), //input wre
        .ad(address), //input [7:0] ad
        .din(data_out) //input [7:0] din
    );

    always_ff @( posedge sys_clk ) begin
        if (sys_nrst) begin
            top_out <= data_in;
        end else if (we) begin
            top_out <= data_out;
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