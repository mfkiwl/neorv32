module neorv32_Fomu_MixedLanguage_ClkGen (
  output wire clk_o,
  output wire rstn_o
);

  wire hf_osc_clk;

  SB_HFOSC #(
    .CLKHF_DIV("0b10") // 12 MHz
  ) HSOSC_inst (
    .CLKHFPU(1'b1),
    .CLKHFEN(1'b1),
    .CLKHF(hf_osc_clk)
  );

  // Settings generated by icepll -i 12 -o 18:
  // F_PLLIN:      12.000 MHz (given)
  // F_PLLOUT:     18.000 MHz (requested)
  // F_PLLOUT:     18.000 MHz (achieved)
  // FEEDBACK:     SIMPLE
  // F_PFD:        12.000 MHz
  // F_VCO:        576.000 MHz
  // DIVR:         0 (4'b0000)
  // DIVF:        47 (7'b0101111)
  // DIVQ:         5 (3'b101)
  // FILTER_RANGE: 1 (3'b001)

  SB_PLL40_CORE #(
    .FEEDBACK_PATH("SIMPLE"),
    .DIVR(4'd0),
    .DIVF(7'd47),
    .DIVQ(3'd5),
    .FILTER_RANGE(3'd1)
  ) Pll_inst (
    .REFERENCECLK(hf_osc_clk),
    .PLLOUTGLOBAL(clk_o),
    .EXTFEEDBACK(1'b0),
    .LOCK(rstn_o),
    .BYPASS(1'b0),
    .RESETB(1'b1),
    .LATCHINPUTVALUE(1'b0),
    .SDI(1'b0),
    .SCLK(1'b0)
  );

endmodule

