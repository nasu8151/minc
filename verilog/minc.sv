module minc (
    input wire CLK,
    input wire nRESET,
    output wire [7:0] pc_out,
    output wire [7:0] top_out,
    output wire [7:0] sp_out
);

    wire [7:0] data;
    reg  [7:0] pc;
    wire [11:0] instruction;

    reg [11:0] rom [0:255];
    `ifdef TEST
    initial $readmemh("test.hex", rom);
    `else
    initial $readmemh("program.hex", rom);
    `endif

    reg [7:0] ram [0:255];
    reg [7:0] sp;

    assign pc_out = pc;
    assign top_out = ram[sp];
    assign sp_out = sp;

    assign instruction = rom[pc];

    always @(posedge CLK or negedge nRESET) begin
        if (nRESET == 0) begin
            // active-low reset: force PC and stack pointer to 0
            pc <= 8'h0;
            sp <= 8'h0;
        end else begin
            // Normal operation: perform instruction (MSB=1 -> ADD, else LD)
            if (instruction[11:8] == 4'b0000) begin// if bits 11:8 are 0000, it's a push instruction
                ram[sp + 1] <= rom[pc[7:0]][7:0];
                sp <= sp + 1;
            end else if (instruction[11:8] == 4'b0100) begin // if bits 11:8 are 0100, it's an ADD instruction
                ram[sp-1] <= ram[sp-1] + ram[sp];
                sp <= sp - 1;
            end else if (instruction[11:8] == 4'b0101) begin // if bits 11:8 are 0101, it's a SUB instruction
                ram[sp-1] <= ram[sp-1] - ram[sp];
                sp <= sp - 1;
            end else if (instruction[11:8] == 4'b0110) begin // if bits 11:8 are 0110, it's a MUL instruction
                ram[sp-1] <= ram[sp-1] * ram[sp];
                sp <= sp - 1;
            end else if (instruction[11:8] == 4'b1000) begin // if bits 11:8 are 1000, it's a HALT instruction
                $finish;
            end
            // Increment PC only during normal operation (not during reset)
            pc <= pc + 1;
        end
    end

endmodule