module ssram #(
    parameter ADDR_WIDTH = 8,
    parameter DATA_WIDTH = 8
)
(
    input  logic        clk,
    input  logic        rst_n,
    input  logic [ADDR_WIDTH-1:0]  addr,
    output logic [DATA_WIDTH-1:0]  dout
);

    // Data ROM: 2^ADDR_WIDTH x DATA_WIDTH-bit
    logic  [DATA_WIDTH-1:0]  ram  [0:(1<<ADDR_WIDTH)-1];
    logic  [ADDR_WIDTH-1:0]  addr_reg;

    // Synchronous read
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            // do nothing on reset
        end else begin
            addr_reg <= addr;
        end
    end

    assign dout = ram[addr_reg];
endmodule