module minc (
    input wire CLK,
    input wire nRESET,
    output wire [7:0] pc_out,
    output wire [7:0] top_out,
    output wire [7:0] sp_out
);

    wire [7:0] data;
    reg  [7:0] pc;
    wire [9:0] instruction;

    reg [9:0] rom [0:255];
    initial $readmemh("program.hex", rom);

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
            if (instruction[9:8] == 2'b00) begin// if bits 9:8 are 00, it's an LD instruction
                stack[sp] <= rom[pc[7:0]][7:0];
                sp <= sp + 1;
            end else if (instruction[9:8] == 2'b01) begin // if bits 9:8 are 01, it's an ADD instruction
                stack[sp-2] <= stack[sp-1] + stack[sp-2];
                sp <= sp - 1;
            end else if (instruction[9:8] == 2'b10) begin // if bits 9:8 are 10, it's a SUB instruction
                stack[sp-2] <= stack[sp-1] - stack[sp-2];
                sp <= sp - 1;
            end else if (instruction[9:8] == 2'b11) begin // if bits 9:8 are 11, it's a MUL instruction
                stack[sp-2] <= stack[sp-1] * stack[sp-2];
                sp <= sp - 1;
            end
            // Increment PC only during normal operation (not during reset)
            pc <= pc + 1;
        end
    end

endmodule