module minc_gw_top (
    input  wire        sys_clk,
    input  wire        sys_nrst,
    output wire [7:0]  pc_out,
    output wire [7:0]  top_out,
    output wire [7:0]  sp_out
);

    logic [23:0] presc_cnt;
    logic        int_clk;

    minc u_minc (
        .CLK    (int_clk),
        .nRESET (sys_nrst),
        .pc_out (pc_out),
        .top_out(top_out),
        .sp_out (sp_out)
    );

    always_ff @(posedge sys_clk or negedge sys_nrst) begin
        if (!sys_nrst) begin
            presc_cnt <= 24'd0;
            int_clk   <= 1'b0;
        end else if (presc_cnt == 24'd13_499_999) begin
            presc_cnt <= 24'd0;
            int_clk   <= ~int_clk;
        end else begin
            presc_cnt <= presc_cnt + 24'd1;
        end
    end

endmodule