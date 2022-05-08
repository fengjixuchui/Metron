`include "metron_tools.sv"

// Private non-const methods should turn into SV tasks.

module Module
(
  input logic clock
);
/*public:*/

  function tock();
    tick();
  endfunction
  always_comb tock();

/*private:*/

  function tick();
  endfunction

  task automatic some_task();
    my_reg <= my_reg + my_reg2 + 3;
    some_task2();
  endtask
  always_ff @(posedge clock) some_task();

  task automatic some_task2();
    my_reg2 <= my_reg2 + 3;
  endtask

  logic[7:0] my_reg;
  logic[7:0] my_reg2;
endmodule
