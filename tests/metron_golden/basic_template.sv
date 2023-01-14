`include "metron_tools.sv"

// Templates can be used for module parameters

module Submod #(parameter int WIDTH,parameter  int HEIGHT = 2)
(
  // output signals
  output logic[WIDTH-1:0] my_width,
  output logic[HEIGHT-1:0] my_height
);
/*public:*/

  always_comb begin : tock
    my_width = (WIDTH)'(100);
    my_height = (HEIGHT)'(200);
  end

endmodule

module Module (
  // tock() ports
  output logic[19:0] tock_ret
);
/*public:*/

  always_comb begin : tock
    tock_ret = submodule_my_width + submodule_my_height;
  end

  Submod #(10,11) submodule(
    // output signals
    .my_width(submodule_my_width),
    .my_height(submodule_my_height)
  );
  logic[10-1:0] submodule_my_width;
  logic[11-1:0] submodule_my_height;

endmodule
