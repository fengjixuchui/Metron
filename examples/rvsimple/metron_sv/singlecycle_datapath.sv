// RISC-V SiMPLE SV -- single-cycle data path
// BSD 3-Clause License
// (c) 2017-2019, Arthur Matos, Marcus Vinicius Lamar, Universidade de Brasília,
//                Marek Materzok, University of Wrocław

`ifndef SINGLECYCLE_DATAPATH_H
`define SINGLECYCLE_DATAPATH_H

`include "adder.sv"
`include "alu.sv"
`include "config.sv"
`include "constants.sv"
`include "immediate_generator.sv"
`include "instruction_decoder.sv"
`include "metron_tools.sv"
`include "multiplexer2.sv"
`include "multiplexer4.sv"
`include "multiplexer8.sv"
`include "regfile.sv"
`include "register.sv"

module singlecycle_datapath (
  // global clock
  input logic clock,
  // input signals
  input logic reset,
  input logic[31:0] data_mem_read_data,
  input logic[31:0] inst,
  input logic pc_write_enable,
  input logic regfile_write_enable,
  input logic alu_operand_a_select,
  input logic alu_operand_b_select,
  input logic[2:0] reg_writeback_select,
  input logic[1:0] next_pc_select,
  input logic[4:0] alu_function,
  // output signals
  output logic[31:0] data_mem_address,
  output logic[31:0] data_mem_write_data,
  output logic[31:0] pc,
  output logic[6:0] inst_opcode,
  output logic[2:0] inst_funct3,
  output logic[6:0] inst_funct7,
  output logic alu_result_equal_zero
);
 /*public:*/


  // control signals

 /*private:*/
  logic[31:0] rs1_data;
  logic[31:0] rs2_data;

  logic[4:0] inst_rd;
  logic[4:0] inst_rs1;
  logic[4:0] inst_rs2;

 /*public:*/
  //----------------------------------------

  always_comb begin : tock_pc pc = program_counter_value; end

  //----------------------------------------

  always_comb begin : tock_instruction_decoder
    idec_inst = inst;

    inst_opcode = idec_inst_opcode;
    inst_funct3 = idec_inst_funct3;
    inst_funct7 = idec_inst_funct7;
    inst_rd = idec_inst_rd;
    inst_rs1 = idec_inst_rs1;
    inst_rs2 = idec_inst_rs2;
  end

  //----------------------------------------

  always_comb begin : tock_immediate_generator
    igen_inst = inst;
  end

  //----------------------------------------

  always_comb begin : tock_reg_read
    regs_rd_address = idec_inst_rd;
    regs_rs1_address = idec_inst_rs1;
    regs_rs2_address = idec_inst_rs2;
    rs1_data = regs_rs1_data;
    rs2_data = regs_rs2_data;
  end

  always_comb begin : tock_mux_operand_a
    mux_operand_a_sel = alu_operand_a_select;
    mux_operand_a_in0 = regs_rs1_data;
    mux_operand_a_in1 = program_counter_value;
  end

  always_comb begin : tock_mux_operand_b
    mux_operand_b_sel = alu_operand_b_select;
    mux_operand_b_in0 = regs_rs2_data;
    mux_operand_b_in1 = igen_immediate;
  end

  always_comb begin : tock_alu
    alu_core_alu_function = alu_function;
    alu_core_operand_a = mux_operand_a_out;
    alu_core_operand_b = mux_operand_b_out;
    alu_result_equal_zero = alu_core_result_equal_zero;
  end

  always_comb begin : tock_adder_pc_plus_4
    adder_pc_plus_4_operand_a = 32'h00000004;
    adder_pc_plus_4_operand_b = program_counter_value;
  end

  always_comb begin : tock_adder_pc_plus_immediate
    adder_pc_plus_immediate_operand_a = program_counter_value;
    adder_pc_plus_immediate_operand_b = igen_immediate;
  end

  always_comb begin : tock_data_mem_write_data
    data_mem_address = alu_core_result;
    data_mem_write_data = regs_rs2_data;
  end

  always_comb begin : tock_mux_next_pc_select
    mux_next_pc_select_sel = next_pc_select;
    mux_next_pc_select_in0 = adder_pc_plus_4_result;
    mux_next_pc_select_in1 = adder_pc_plus_immediate_result;
    mux_next_pc_select_in2 = {alu_core_result[31:1], 1'b0};
    mux_next_pc_select_in3 = 32'b0;
  end

  always_comb begin : tock_program_counter
    program_counter_reset = reset;
    program_counter_write_enable = pc_write_enable;
    program_counter_next = mux_next_pc_select_out;
  end

  always_comb begin : tock_mux_reg_writeback
    mux_reg_writeback_sel = reg_writeback_select;
    mux_reg_writeback_in0 = alu_core_result;
    mux_reg_writeback_in1 = data_mem_read_data;
    mux_reg_writeback_in2 = adder_pc_plus_4_result;
    mux_reg_writeback_in3 = igen_immediate;
    mux_reg_writeback_in4 = 32'b0;
    mux_reg_writeback_in5 = 32'b0;
    mux_reg_writeback_in6 = 32'b0;
    mux_reg_writeback_in7 = 32'b0;
  end

  always_comb begin : tock_reg_writeback
    regs_write_enable = regfile_write_enable;
    regs_rd_data = mux_reg_writeback_out;
  end

  //----------------------------------------

 /*private:*/
  adder #(
    // Template Parameters
    .WIDTH(32)
  ) adder_pc_plus_4(
    // Input signals
    .operand_a(adder_pc_plus_4_operand_a),
    .operand_b(adder_pc_plus_4_operand_b),
    // Output signals
    .result(adder_pc_plus_4_result)
  );
  logic[32-1:0] adder_pc_plus_4_operand_a;
  logic[32-1:0] adder_pc_plus_4_operand_b;
  logic[32-1:0] adder_pc_plus_4_result;

  adder #(
    // Template Parameters
    .WIDTH(32)
  ) adder_pc_plus_immediate(
    // Input signals
    .operand_a(adder_pc_plus_immediate_operand_a),
    .operand_b(adder_pc_plus_immediate_operand_b),
    // Output signals
    .result(adder_pc_plus_immediate_result)
  );
  logic[32-1:0] adder_pc_plus_immediate_operand_a;
  logic[32-1:0] adder_pc_plus_immediate_operand_b;
  logic[32-1:0] adder_pc_plus_immediate_result;

  alu alu_core(
    // Input signals
    .alu_function(alu_core_alu_function),
    .operand_a(alu_core_operand_a),
    .operand_b(alu_core_operand_b),
    // Output signals
    .result(alu_core_result),
    .result_equal_zero(alu_core_result_equal_zero)
  );
  logic[4:0] alu_core_alu_function;
  logic[31:0] alu_core_operand_a;
  logic[31:0] alu_core_operand_b;
  logic[31:0] alu_core_result;
  logic alu_core_result_equal_zero;

  multiplexer4 #(
    // Template Parameters
    .WIDTH(32)
  ) mux_next_pc_select(
    // Input signals
    .in0(mux_next_pc_select_in0),
    .in1(mux_next_pc_select_in1),
    .in2(mux_next_pc_select_in2),
    .in3(mux_next_pc_select_in3),
    .sel(mux_next_pc_select_sel),
    // Output signals
    .out(mux_next_pc_select_out)
  );
  logic[32-1:0] mux_next_pc_select_in0;
  logic[32-1:0] mux_next_pc_select_in1;
  logic[32-1:0] mux_next_pc_select_in2;
  logic[32-1:0] mux_next_pc_select_in3;
  logic[1:0] mux_next_pc_select_sel;
  logic[32-1:0] mux_next_pc_select_out;

  multiplexer2 #(
    // Template Parameters
    .WIDTH(32)
  ) mux_operand_a(
    // Input signals
    .in0(mux_operand_a_in0),
    .in1(mux_operand_a_in1),
    .sel(mux_operand_a_sel),
    // Output signals
    .out(mux_operand_a_out)
  );
  logic[32-1:0] mux_operand_a_in0;
  logic[32-1:0] mux_operand_a_in1;
  logic mux_operand_a_sel;
  logic[32-1:0] mux_operand_a_out;

  multiplexer2 #(
    // Template Parameters
    .WIDTH(32)
  ) mux_operand_b(
    // Input signals
    .in0(mux_operand_b_in0),
    .in1(mux_operand_b_in1),
    .sel(mux_operand_b_sel),
    // Output signals
    .out(mux_operand_b_out)
  );
  logic[32-1:0] mux_operand_b_in0;
  logic[32-1:0] mux_operand_b_in1;
  logic mux_operand_b_sel;
  logic[32-1:0] mux_operand_b_out;

  multiplexer8 #(
    // Template Parameters
    .WIDTH(32)
  ) mux_reg_writeback(
    // Input signals
    .in0(mux_reg_writeback_in0),
    .in1(mux_reg_writeback_in1),
    .in2(mux_reg_writeback_in2),
    .in3(mux_reg_writeback_in3),
    .in4(mux_reg_writeback_in4),
    .in5(mux_reg_writeback_in5),
    .in6(mux_reg_writeback_in6),
    .in7(mux_reg_writeback_in7),
    .sel(mux_reg_writeback_sel),
    // Output signals
    .out(mux_reg_writeback_out)
  );
  logic[32-1:0] mux_reg_writeback_in0;
  logic[32-1:0] mux_reg_writeback_in1;
  logic[32-1:0] mux_reg_writeback_in2;
  logic[32-1:0] mux_reg_writeback_in3;
  logic[32-1:0] mux_reg_writeback_in4;
  logic[32-1:0] mux_reg_writeback_in5;
  logic[32-1:0] mux_reg_writeback_in6;
  logic[32-1:0] mux_reg_writeback_in7;
  logic[2:0] mux_reg_writeback_sel;
  logic[32-1:0] mux_reg_writeback_out;

  single_register #(
    // Template Parameters
    .WIDTH(32),
    .INITIAL(rv_config::INITIAL_PC)
  ) program_counter(
    // Global clock
    .clock(clock),
    // Input signals
    .reset(program_counter_reset),
    .write_enable(program_counter_write_enable),
    .next(program_counter_next),
    // Output registers
    .value(program_counter_value)
  );
  logic program_counter_reset;
  logic program_counter_write_enable;
  logic[32-1:0] program_counter_next;
  logic[32-1:0] program_counter_value;

  regfile regs(
    // Global clock
    .clock(clock),
    // Input signals
    .write_enable(regs_write_enable),
    .rd_address(regs_rd_address),
    .rs1_address(regs_rs1_address),
    .rs2_address(regs_rs2_address),
    .rd_data(regs_rd_data),
    // Output signals
    .rs1_data(regs_rs1_data),
    .rs2_data(regs_rs2_data)
  );
  logic regs_write_enable;
  logic[4:0] regs_rd_address;
  logic[4:0] regs_rs1_address;
  logic[4:0] regs_rs2_address;
  logic[31:0] regs_rd_data;
  logic[31:0] regs_rs1_data;
  logic[31:0] regs_rs2_data;

  instruction_decoder idec(
    // Input signals
    .inst(idec_inst),
    // Output signals
    .inst_opcode(idec_inst_opcode),
    .inst_funct3(idec_inst_funct3),
    .inst_funct7(idec_inst_funct7),
    .inst_rd(idec_inst_rd),
    .inst_rs1(idec_inst_rs1),
    .inst_rs2(idec_inst_rs2)
  );
  logic[31:0] idec_inst;
  logic[6:0] idec_inst_opcode;
  logic[2:0] idec_inst_funct3;
  logic[6:0] idec_inst_funct7;
  logic[4:0] idec_inst_rd;
  logic[4:0] idec_inst_rs1;
  logic[4:0] idec_inst_rs2;

  immediate_generator igen(
    // Input signals
    .inst(igen_inst),
    // Output signals
    .immediate(igen_immediate)
  );
  logic[31:0] igen_inst;
  logic[31:0] igen_immediate;

endmodule

`endif // SINGLECYCLE_DATAPATH_H
