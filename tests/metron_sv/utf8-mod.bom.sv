`include "metron_tools.sv"

// UTF-8 text files with a byte order mark should be supported.

// From https://www.w3.org/2001/06/utf-8-test/UTF-8-demo.html:

/*
Runes:

  ᚻᛖ ᚳᚹᚫᚦ ᚦᚫᛏ ᚻᛖ ᛒᚢᛞᛖ ᚩᚾ ᚦᚫᛗ ᛚᚪᚾᛞᛖ ᚾᚩᚱᚦᚹᛖᚪᚱᛞᚢᛗ ᚹᛁᚦ ᚦᚪ ᚹᛖᛥᚫ

  (Old English, which transcribed into Latin reads 'He cwaeth that he
  bude thaem lande northweardum with tha Westsae.' and means 'He said
  that he lived in the northern land near the Western Sea.')
*/

module Module
(
  // get_reg() ports
  output logic[7:0] get_reg_ret
);
/*public:*/

  initial begin
    my_reg = 7;
  end

  always_comb begin : get_reg
    get_reg_ret = my_reg;
  end

/*private:*/

  logic[7:0] my_reg;
endmodule
