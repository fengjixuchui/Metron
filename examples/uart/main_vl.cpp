#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "Platform.h"
#include "Tests.h"
#include "metron_vl/Vuart_top.h"

//------------------------------------------------------------------------------
// Note that you have to build the Verilator version with "repeat_msg = 1" on
// the Verilator command line for this benchmark to match the C version.

void benchmark() {
  const int cycles_per_bit = 3;
  const int repeat_msg = 1;
  const int cycle_max = 10000000;

  Vuart_top vtop;
  vtop.tock_reset = 1;
  vtop.clock = 0;
  vtop.eval();
  vtop.clock = 1;
  vtop.eval();

  // Reset done, clock starts low.
  vtop.clock = 0;
  vtop.tock_reset = 0;
  vtop.eval();

  auto time_a = timestamp();
  for (int cycle = 0; cycle < cycle_max; cycle++) {
    vtop.clock = 1;
    vtop.eval();
    vtop.clock = 0;
    vtop.eval();
  }
  auto time_b = timestamp();

  double delta_sec = (double(time_b) - double(time_a)) / 1000000000.0;
  double rate = double(cycle_max) / delta_sec;
  printf("Simulation rate %f Mhz\n", rate / 1000000.0);
 }

//------------------------------------------------------------------------------

TestResults test_lockstep(int argc, char** argv) {
  TEST_INIT("Metron+Verilator lockstep simulation:");

  // Synchronous reset cycle.
  Vuart_top vtop;
  vtop.tock_reset = 1;
  vtop.clock = 0;
  vtop.eval();
  vtop.clock = 1;
  vtop.eval();

  // Reset done, clock starts low.
  vtop.clock = 0;
  vtop.tock_reset = 0;
  vtop.eval();

  LOG_B("========================================\n");

  int cycle;
  for (cycle = 0; cycle < 50000; cycle++) {
    bool old_valid = vtop.get_valid_ret;
    vtop.clock = 1;
    vtop.eval();
    vtop.clock = 0;
    vtop.eval();

    if (!old_valid && vtop.get_valid_ret) LOG_B("%c", (uint8_t)vtop.get_data_out_ret);
    if (vtop.get_done_ret) {
      printf("vtop done!\n");
      break;
    }
  }

  LOG_B("\n");
  LOG_B("========================================\n");
  LOG_B("%d\n", cycle);
  EXPECT_EQ(vtop.get_checksum_ret, 0x0000b764, "Verilator uart checksum fail");

  TEST_DONE();
}

//------------------------------------------------------------------------------

int main(int argc, char** argv) {
  printf("Running Verilated Metron uart test\n");

  //benchmark();

  TestResults results("main");
  results << test_lockstep(argc, argv);

  if (results.test_fail) {
    printf("Some tests fail.\n");
    return 1;
  } else {
    printf("All tests pass\n");
    return 0;
  }
}

//------------------------------------------------------------------------------
