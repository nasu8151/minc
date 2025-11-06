module minc (
    input wire CLK,
    input wire nRESET,
    output wire [7:0] pc_out,
    output wire [7:0] acc_out
);

    wire [7:0] data;
    reg  [7:0] pc;
    wire [9:0] instruction;

    reg [7:0] accumulator;

    reg [9:0] rom [0:255];
    initial $readmemh("program.hex", rom);

    assign pc_out = pc;
    assign acc_out = accumulator;

    assign instruction = rom[pc];

    always @(posedge CLK or negedge nRESET) begin
        if (nRESET == 0) begin
            // active-low reset: force PC and accumulator to 0
            pc <= 8'h0;
            accumulator <= 8'h0;
        end else begin
            // Normal operation: perform instruction (MSB=1 -> ADD, else LD)
            if (instruction[9:8] == 2'b00) // if bits 9:8 are 00, it's an LD instruction
                accumulator <= rom[pc[7:0]][7:0];
            else if (instruction[9:8] == 2'b01) // if bits 9:8 are 01, it's an ADD instruction
                accumulator <= accumulator + rom[pc[7:0]][7:0];
            else if (instruction[9:8] == 2'b10) // if bits 9:8 are 10, it's a SUB instruction
                accumulator <= accumulator - rom[pc[7:0]][7:0];

            // Increment PC only during normal operation (not during reset)
            pc <= pc + 1;
        end
    end

endmodule