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
    input  logic        ce,
    input  logic        we,
    output logic [DATA_WIDTH-1:0]  dout,
    input  logic [ADDR_WIDTH-1:0]  dbg_addr0,
    output logic [DATA_WIDTH-1:0]  dbg_dout0,
    input  logic [ADDR_WIDTH-1:0]  dbg_addr1,
    output logic [DATA_WIDTH-1:0]  dbg_dout1
);

    // Data RAM: 256 x 8-bit
    logic  [DATA_WIDTH-1:0]  ram  [0:(1<<ADDR_WIDTH)-1];

    // Synchronous read/write
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            // do nothing on reset
        end else begin
            dout <= ce ? ram[addr] : 'hz;
            if (we) begin
                ram[addr] <= din;
            end
        end
    end

    assign dbg_dout0 = ram[dbg_addr0];
    assign dbg_dout1 = ram[dbg_addr1];
endmodule

`endif // __SSRAM_SV__