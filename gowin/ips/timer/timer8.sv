module TIMER8 (
    input  logic       I_CLK,
    input  logic       I_RESETN,

    input  logic       I_TX_EN,
    input  logic [2:0] I_WADDR,
    input  logic [7:0] I_WDATA,

    input  logic       I_RX_EN,
    input  logic [2:0] I_RADDR,
    output logic [7:0] O_RDATA,

    output logic       O_OVERFLOW, // 1-clock pulse when COUNTER reaches OVERFLOW
    output logic       O_COMPARE,  // 1-clock pulse when COUNTER reaches COMPARE
    output logic       O_OVF_INT,  // level, held while STATUS.OVF & CONFIG.IE_OVF
    output logic       O_CMP_INT   // level, held while STATUS.CMP & CONFIG.IE_CMP
);

    // Register map (I_WADDR/I_RADDR, 3 bits):
    //   0 CONFIG   RW  bit0=EN, bit1=IE_OVF, bit2=IE_CMP, bit[6:3]=prescale select (/1,/2,/4,.../1024)
    //   1 COMPARE  RW  compare threshold
    //   2 OVERFLOW RW  period (TOP) threshold; COUNTER reloads to 0 on reaching this value
    //   3 COUNTER  RW  current count, software-presettable
    //   4 STATUS   RW  bit0=OVF pending, bit1=CMP pending; write 1 to a bit clears it (W1C)
    //   5-7        -   reserved, read 0, writes ignored
    localparam logic [2:0] ADDR_CONFIG   = 3'h0;
    localparam logic [2:0] ADDR_COMPARE  = 3'h1;
    localparam logic [2:0] ADDR_OVERFLOW = 3'h2;
    localparam logic [2:0] ADDR_COUNTER  = 3'h3;
    localparam logic [2:0] ADDR_STATUS   = 3'h4;

    logic [7:0] config_reg;
    logic [7:0] compare_reg;
    logic [7:0] overflow_reg;
    logic [7:0] counter_reg;
    logic [7:0] status_reg; // only bit0/bit1 used

    wire en                    = config_reg[0];
    wire ie_ovf                = config_reg[1];
    wire ie_cmp                = config_reg[2];
    wire [3:0] prescale_sel    = config_reg[6:3];

    assign O_OVF_INT = status_reg[0] & ie_ovf;
    assign O_CMP_INT = status_reg[1] & ie_cmp;

    wire write_config   = I_TX_EN && (I_WADDR == ADDR_CONFIG);
    wire write_compare  = I_TX_EN && (I_WADDR == ADDR_COMPARE);
    wire write_overflow = I_TX_EN && (I_WADDR == ADDR_OVERFLOW);
    wire write_counter  = I_TX_EN && (I_WADDR == ADDR_COUNTER);
    wire write_status   = I_TX_EN && (I_WADDR == ADDR_STATUS);

    // prescaler: generates one `tick` every 2^prescale_sel clocks while EN
    logic [9:0] prescale_cnt;
    wire [9:0] prescale_max = (10'h1 << prescale_sel) - 10'h1;
    wire tick = en && (prescale_cnt == prescale_max);

    always_ff @(posedge I_CLK or negedge I_RESETN) begin
        if (!I_RESETN) begin
            prescale_cnt <= 10'h0;
        end else if (!en) begin
            prescale_cnt <= 10'h0;
        end else if (tick) begin
            prescale_cnt <= 10'h0;
        end else begin
            prescale_cnt <= prescale_cnt + 10'h1;
        end
    end

    wire ovf_match = tick && (counter_reg == overflow_reg);
    wire cmp_match = tick && (counter_reg == compare_reg);

    always_ff @(posedge I_CLK or negedge I_RESETN) begin
        if (!I_RESETN) begin
            config_reg   <= 8'h0;
            compare_reg  <= 8'h0;
            overflow_reg <= 8'h0;
            counter_reg  <= 8'h0;
            status_reg   <= 8'h0;
            O_OVERFLOW   <= 1'b0;
            O_COMPARE    <= 1'b0;
        end else begin
            O_OVERFLOW <= ovf_match;
            O_COMPARE  <= cmp_match;

            if (write_config)   config_reg   <= I_WDATA & 8'h3F;
            if (write_compare)  compare_reg  <= I_WDATA;
            if (write_overflow) overflow_reg <= I_WDATA;

            if (write_counter) counter_reg <= I_WDATA;      // software preset wins over tick
            else if (ovf_match) counter_reg <= 8'h0;
            else if (tick) counter_reg <= counter_reg + 8'h1;

            // a same-cycle hardware match always wins over a software clear,
            // so a newly-pending interrupt is never silently lost.
            status_reg[0] <= ovf_match ? 1'b1 : (write_status && I_WDATA[0] ? 1'b0 : status_reg[0]);
            status_reg[1] <= cmp_match ? 1'b1 : (write_status && I_WDATA[1] ? 1'b0 : status_reg[1]);
            status_reg[7:2] <= 6'h0;
        end
    end

    always_ff @(posedge I_CLK or negedge I_RESETN) begin
        if (!I_RESETN) begin
            O_RDATA <= 8'h0;
        end else if (I_RX_EN) begin
            case (I_RADDR)
                ADDR_CONFIG:   O_RDATA <= config_reg & 8'h3F;
                ADDR_COMPARE:  O_RDATA <= compare_reg;
                ADDR_OVERFLOW: O_RDATA <= overflow_reg;
                ADDR_COUNTER:  O_RDATA <= counter_reg;
                ADDR_STATUS:   O_RDATA <= status_reg & 8'h03;
                default:       O_RDATA <= 8'h00;
            endcase
        end
    end

endmodule
