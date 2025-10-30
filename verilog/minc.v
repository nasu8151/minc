module minc (
    input wire CLK,
    input wire nRESET,
    output wire [7:0] pc_out,
    output wire [7:0] acc_out
);

    wire [7:0] data;
    reg  [7:0] pc;

    reg [7:0] accumulator;

    reg [8:0] rom [0:255];
    initial $readmemh("program.hex", rom);

    assign pc_out = pc;
    assign acc_out = accumulator;

    always @(posedge CLK or negedge nRESET) begin
        if (nRESET == 0) begin
            pc <= 8'h0;
            accumulator <= 8'h0;
        end else begin
            if (rom[pc[7:0]][8]) // if MSB is 1, it's an ADD instruction
                accumulator <= accumulator + rom[pc[7:0]][7:0];
            else // else it's a LD instruction
                accumulator <= rom[pc[7:0]][7:0];
        end
            pc <= pc + 1;
    end

endmodule