`ifndef UART_HELLO_H
`define UART_HELLO_H

`include "metron_tools.sv"

//==============================================================================

module uart_hello
#(parameter int repeat_msg = 0)
(
  // global clock
  input logic clock,
  // data() ports
  output logic[7:0] data_ret,
  // req() ports
  output logic req_ret,
  // done() ports
  output logic done_ret,
  // tick() ports
  input logic tick_i_rstn,
  input logic tick_i_cts,
  input logic tick_i_idle
);
/*public:*/
  initial begin
    $readmemh("examples/uart/message.hex", _memory, 0, 511);
  end

  always_comb begin : data
    data_ret = _data;
  end

  always_comb begin : req
    req_ret = _state == SEND;
  end

  always_comb begin : done
    done_ret = _state == DONE;
  end

  always_ff @(posedge clock) begin : tick
    if (!tick_i_rstn) begin
      _state <= WAIT;
      _cursor <= 0;
    end else begin
      _data <= _memory[_cursor];
      if (_state == WAIT && tick_i_idle) begin
        _state <= SEND;
      end else if (_state == SEND && tick_i_cts) begin
        if (_cursor == 9'(message_len - 1)) begin
          _state <= DONE;
        end else begin
          _cursor <= _cursor + 1;
        end
      end else if (_state == DONE) begin
        if (repeat_msg) _state <= WAIT;
        _cursor <= 0;
      end
    end
  end

/*private:*/
  localparam int message_len = 512;
  localparam int cursor_bits = $clog2(message_len);

  localparam int WAIT = 0;
  localparam int SEND = 1;
  localparam int DONE = 2;

  logic[1:0] _state;
  logic[cursor_bits-1:0] _cursor;
  logic[7:0] _memory[512];
  logic[7:0] _data;
endmodule

//==============================================================================

`endif // UART_HELLO_H
