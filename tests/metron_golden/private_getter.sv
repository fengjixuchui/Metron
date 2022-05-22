`include "metron_tools.sv"

// Private getter methods are OK

module Module
(
  // output signals
  output int my_sig,
  // tock() ports
  output int tock_ret
);
/*public:*/


  always_comb begin : tock
    my_sig = my_getter();
    tock_ret = my_sig;
  end

/*private:*/

  function int my_getter();
    my_getter = 12;
  endfunction

endmodule
