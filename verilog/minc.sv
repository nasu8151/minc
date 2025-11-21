module minc (
    input wire CLK,
    input wire nRESET,
    output wire [7:0] pc_out,
    output wire [7:0] top_out,
    output wire [7:0] sp_out
);

    wire [7:0] data;
    reg  [7:0] pc;
    wire [10:0] instruction;

    reg [10:0] rom [0:255];
    `ifdef TEST
    initial $readmemh("test.hex", rom);
    `else
    initial $readmemh("program.hex", rom);
    `endif

    reg [7:0] stack [0:255];
    reg [7:0] sp;

    assign pc_out = pc;
    assign top_out = stack[sp - 1];
    assign sp_out = sp;

    assign instruction = rom[pc];

    always @(posedge CLK or negedge nRESET) begin
        if (nRESET == 0) begin
            // active-low reset: force PC and stack pointer to 0
            pc <= 8'h0;
            sp <= 8'h0;
        end else begin
            // Normal operation: perform instruction (MSB=1 -> ADD, else LD)
            if (instruction[10:8] == 3'b000) begin// if bits 10:8 are 000, it's an LD instruction
                stack[sp] <= rom[pc[7:0]][7:0];
                sp <= sp + 1;
            end else if (instruction[10:8] == 3'b001) begin // if bits 10:8 are 001, it's an ADD instruction
                stack[sp-2] <= stack[sp-2] + stack[sp-1];
                sp <= sp - 1;
            end else if (instruction[10:8] == 3'b010) begin // if bits 10:8 are 010, it's a SUB instruction
                stack[sp-2] <= stack[sp-2] - stack[sp-1];
                sp <= sp - 1;
            end else if (instruction[10:8] == 3'b011) begin // if bits 10:8 are 011, it's a MUL instruction
                stack[sp-2] <= stack[sp-2] * stack[sp-1];
                sp <= sp - 1;
            end else if (instruction[10:8] == 3'b100) begin // if bits 10:8 are 100, it's a HALT instruction
                $finish;
            end
            // Increment PC only during normal operation (not during reset)
            pc <= pc + 1;
        end
    end

endmodule