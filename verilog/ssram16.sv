`ifndef __SSRAM16_SV__
`define __SSRAM16_SV__

// Byte-addressed, 16-bit wide RAM for minc-16.
//
// Modelled as two independent 8-bit lanes with their own write enables, which
// is exactly how it maps onto Gowin block RAM (two 8-bit-wide SP instances with
// separate `wre`) -- no dedicated byte-enable primitive is required.
module ssram16 #(
    parameter ADDR_WIDTH = 16   // width of the *byte* address
)
(
    input  logic                    clk,
    input  logic                    rst_n,
    input  logic [ADDR_WIDTH-1:0]   addr,   // byte address; addr[0] picks the lane
    input  logic [15:0]             din,
    input  logic                    ce,
    input  logic [1:0]              we,     // we[0] = even byte, we[1] = odd byte
    output logic [15:0]             dout,
    input  logic [ADDR_WIDTH-1:0]   dbg_addr0,
    output logic [15:0]             dbg_dout0
);

    localparam WORDS = 1 << (ADDR_WIDTH - 1);

    logic [7:0] mem_lo [0:WORDS-1];
    logic [7:0] mem_hi [0:WORDS-1];

    wire [ADDR_WIDTH-2:0] widx     = addr[ADDR_WIDTH-1:1];
    wire [ADDR_WIDTH-2:0] dbg_widx = dbg_addr0[ADDR_WIDTH-1:1];

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            // do nothing on reset
        end else begin
            dout <= ce ? {mem_hi[widx], mem_lo[widx]} : 16'hzzzz;
            if (we[0]) mem_lo[widx] <= din[7:0];
            if (we[1]) mem_hi[widx] <= din[15:8];
        end
    end

    assign dbg_dout0 = {mem_hi[dbg_widx], mem_lo[dbg_widx]};

endmodule

`endif // __SSRAM16_SV__
