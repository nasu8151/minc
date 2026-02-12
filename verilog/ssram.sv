`ifndef __SSRAM_SV__
`define __SSRAM_SV__

module ssram #(
    parameter ADDR_WIDTH = 8,
    parameter DATA_WIDTH = 8
)
(
    input  logic        clk,
    input  logic        rst_n,
    input  logic [ADDR_WIDTH-1:0]  addr,
    input  logic [DATA_WIDTH-1:0]  din,
    input  logic        we,
    output logic [DATA_WIDTH-1:0]  dout,
    input  logic [ADDR_WIDTH-1:0]  dbg_addr,
    output logic [DATA_WIDTH-1:0]  dbg_dout
);

    // Data RAM: 256 x 8-bit
    logic  [DATA_WIDTH-1:0]  ram  [0:(1<<ADDR_WIDTH)-1];
    logic  [ADDR_WIDTH-1:0]  addr_reg;

    // Synchronous read/write
    always_ff @(negedge clk or negedge rst_n) begin
        if (!rst_n) begin
            // do nothing on reset
        end else begin
            addr_reg <= addr;
            if (we) begin
                ram[addr] <= din;
            end
        end
    end

    assign dout = ram[addr_reg];
    assign dbg_dout = ram[dbg_addr];
endmodule

`endif // __SSRAM_SV__