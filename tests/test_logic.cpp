#include "metron_tools.h"

#include "test_utils.h"

//------------------------------------------------------------------------------

TestResults test_logic_truncate() {
  TEST_INIT();

  logic<10> a = 0b1111111111;
  logic<5> b = 0;

  b = a + 0;
  EXPECT_EQ(b, 0b11111,
            "Assignment after addition should compile and truncate.");

  TEST_DONE();
}

TestResults test_logic_add() {
  TEST_INIT();

  logic<8> a = 100;
  logic<8> b = 72;
  logic<8> c = a + b;

  EXPECT_EQ(c, 172, "Bad addition?");

  TEST_DONE();
}

TestResults test_logic_cat() {
  TEST_INIT("test_logic_cat");

  logic<5> x = cat(logic<2>(0b10), logic<3>(0b101));

  EXPECT_EQ(x[4], 1, "x");
  EXPECT_EQ(x[3], 0, "x");
  EXPECT_EQ(x[2], 1, "x");
  EXPECT_EQ(x[1], 0, "x");
  EXPECT_EQ(x[0], 1, "x");

  TEST_DONE();
}

TestResults test_logic_slice() {
  TEST_INIT();

  logic<12> a = 0x00000000;

  a = 0;
  slice<7, 0>(a) = 0xFF;
  EXPECT_EQ(0x00FF, a, "x");
  a = 0;
  slice<8, 1>(a) = 0xFF;
  EXPECT_EQ(0x01FE, a, "x");
  a = 0;
  slice<9, 2>(a) = 0xFF;
  EXPECT_EQ(0x03FC, a, "x");
  a = 0;
  slice<10, 3>(a) = 0xFF;
  EXPECT_EQ(0x07F8, a, "x");
  a = 0;
  slice<11, 4>(a) = 0xFF;
  EXPECT_EQ(0x0FF0, a, "x");
  a = 0;
  slice<12, 5>(a) = 0xFF;
  EXPECT_EQ(0x0FE0, a, "x");
  a = 0;
  slice<13, 6>(a) = 0xFF;
  EXPECT_EQ(0x0FC0, a, "x");
  a = 0;
  slice<14, 7>(a) = 0xFF;
  EXPECT_EQ(0x0F80, a, "x");
  a = 0;
  slice<15, 8>(a) = 0xFF;
  EXPECT_EQ(0x0F00, a, "x");

  logic<20> b = 0x0FF00;

  EXPECT_EQ(0x00FF, b12(b, 8), "x");
  EXPECT_EQ(0x01FE, b12(b, 7), "x");
  EXPECT_EQ(0x03FC, b12(b, 6), "x");
  EXPECT_EQ(0x07F8, b12(b, 5), "x");
  EXPECT_EQ(0x0FF0, b12(b, 4), "x");
  EXPECT_EQ(0x0FE0, b12(b, 3), "x");
  EXPECT_EQ(0x0FC0, b12(b, 2), "x");
  EXPECT_EQ(0x0F80, b12(b, 1), "x");
  EXPECT_EQ(0x0F00, b12(b, 0), "x");

  TEST_DONE();
}

TestResults test_logic_dup() {
  TEST_INIT();
  logic<3> boop = 0b101;
  logic<9> moop = dup<3>(boop);
  EXPECT_EQ(moop, 0b101101101, "x");
  TEST_DONE();
}

//------------------------------------------------------------------------------

TestResults test_logic() {
  TEST_INIT("Test logic<> behavior and translation");

  results << test_logic_truncate();
  results << test_logic_add();
  results << test_logic_cat();
  results << test_logic_slice();
  results << test_logic_dup();

  TEST_DONE();
}

//------------------------------------------------------------------------------
