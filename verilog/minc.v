module minc (
    input wire CLK,
    input wire nRESET,
    output wire [15:0] pc_out,
    output wire [7:0] acc_out
);

    wire [7:0] data;
    reg  [7:0] pc;

    reg [7:0] accumulator;

    reg [7:0] rom [0:255];
    initial $readmemh("program.hex", rom);

    assign pc_out = pc;
    assign acc_out = accumulator;

    always @(posedge CLK or negedge nRESET) begin
        if (!nRESET) begin
            pc <= 8'h0;
            accumulator <= 8'h0;
        end else begin
            accumulator <= rom[pc[7:0]];
            pc <= pc + 1;
        end
    end

endmodule