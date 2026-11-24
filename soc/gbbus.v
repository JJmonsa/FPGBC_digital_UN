// Ubicación: FPGBC_digital_UN/soc/gbbus.v
module gbbus (
    input  wire        clk,
    input  wire        resetn,
    input  wire        cs,        // 1 = el procesador está hablando con este periférico
    input  wire        wr,        // 1 = el procesador ESCRIBE
    input  wire        rd,        // 1 = el procesador LEE
    input  wire [4:0]  addr,      // registro: 0x00 ADDR, 0x04 WDATA, 0x08 RDATA, 0x0C CTRL, 0x10 STATUS
    input  wire [31:0] d_in,      // dato que manda el procesador
    output reg  [31:0] d_out,     // dato que devolvemos al procesador
    output reg  [15:0] gb_a,      // dirección hacia el cartucho
    output reg  [7:0]  gb_d_out,  // dato hacia el cartucho
    input  wire [7:0]  gb_d_in,   // dato que llega desde el cartucho
    output wire        gb_d_oe,   // 1 = nosotros ponemos datos en el bus
    output wire        gb_dir,    // sentido del buffer: 1 = hacia el cartucho
    output reg         gb_cs_n,
    output reg         gb_rd_n,
    output reg         gb_wr_n
);
    localparam SETUP  = 3;   // ciclos de espera con la dirección ya puesta (75 ns @ 40 MHz)
    localparam STROBE = 6;   // ciclos que dura el pulso RD o WR (150 ns @ 40 MHz)

    reg [15:0] addr_r;
    reg [7:0]  wdata_r;
    reg [7:0]  rdata_r;
    reg        op_wr;        // 1 = escribir, 0 = leer
    reg        busy;
    reg [1:0]  st;
    reg [3:0]  t;

    assign gb_dir  = op_wr;
    assign gb_d_oe = busy & op_wr;

    always @(posedge clk) begin
        if (!resetn) begin
            busy <= 0; st <= 0; t <= 0;
            addr_r <= 0; wdata_r <= 0; rdata_r <= 0; op_wr <= 0;
            gb_cs_n <= 1; gb_rd_n <= 1; gb_wr_n <= 1;
            gb_a <= 0; gb_d_out <= 0;
        end
        else if (!busy) begin
            gb_cs_n <= 1; gb_rd_n <= 1; gb_wr_n <= 1;
            if (cs && wr) begin
                case (addr)
                    5'h00: addr_r  <= d_in[15:0];
                    5'h04: wdata_r <= d_in[7:0];
                    5'h0C: begin
                        op_wr <= d_in[0];
                        if (d_in[1]) begin        // bit 1 = arrancar operación
                            busy     <= 1;
                            st       <= 0;
                            t        <= 0;
                            gb_a     <= addr_r;
                            gb_d_out <= wdata_r;
                        end
                    end
                    default: ;
                endcase
            end
        end
        else begin
            t <= t + 1;
            case (st)
                2'd0: if (t == SETUP) begin st <= 1; t <= 0; end
                2'd1: begin
                    gb_cs_n <= 0;
                    gb_rd_n <= op_wr;      // leer: RD activo (0)
                    gb_wr_n <= ~op_wr;     // escribir: WR activo (0)
                    if (t == STROBE) begin st <= 2; t <= 0; end
                end
                2'd2: begin
                    gb_cs_n <= 1; gb_rd_n <= 1; gb_wr_n <= 1;
                    rdata_r <= gb_d_in;    // capturar dato recibido del cartucho
                    busy    <= 0;
                end
                default: busy <= 0;
            endcase
        end
    end

    always @(*) begin
        case (addr)
            5'h08:   d_out = {24'd0, rdata_r};
            5'h10:   d_out = {31'd0, busy};
            default: d_out = 32'd0;
        endcase
    end
endmodule
