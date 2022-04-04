#include "metron_tools.h"

// Declaration order _matters_ - a tock() that reads a reg before the tick()
// that writes it is OK.

class Module {
public:

  void tock() {
    sig = reg;
  }

  void tick() {
    reg = 1;
  }

  logic<1> sig;
  logic<1> reg;
};
