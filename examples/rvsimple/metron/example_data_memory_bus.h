// RISC-V SiMPLE SV -- data memory bus
// BSD 3-Clause License
// (c) 2017-2019, Arthur Matos, Marcus Vinicius Lamar, Universidade de Brasília,
//                Marek Materzok, University of Wrocław

#ifndef EXAMPLE_DATA_MEMORY_BUS
#define EXAMPLE_DATA_MEMORY_BUS

#include "config.h"
#include "constants.h"
#include "example_data_memory.h"
#include "metron_tools.h"

class example_data_memory_bus {
 public:
  logic<32> address;     // in
  logic<32> read_data;   // out
  logic<32> write_data;  // in
  logic<4> byte_enable;  // in
  logic<1> read_enable;  // in
  logic<1> write_enable; // in

  example_data_memory_bus(const char* filename = nullptr)
  : data_memory(filename) {
  }

 private:
  example_data_memory data_memory;

 public:
  void tock() {
    logic<1> is_data_memory =
        address >= rv_config::DATA_BEGIN && rv_config::DATA_END >= address;

    data_memory.address = bx<rv_config::DATA_BITS - 2>(address, 2);
    data_memory.byteena = byte_enable;
    data_memory.data = write_data;
    data_memory.wren = write_enable & is_data_memory;
    data_memory.tock();

    logic<32> fetched = data_memory.q;
    read_data = read_enable && is_data_memory ? fetched : b32(DONTCARE);
  }
};

#endif // EXAMPLE_DATA_MEMORY_BUS
