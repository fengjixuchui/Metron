`include "metron_tools.sv"

// Most kinds of C++ enum declarations should work.

// bad
// enum { FOO, BAR, BAZ };
// typedef enum logic[1:0] { FOO=70, BAR=71, BAZ=72 } blem;
// typedef enum { FOO, BAR=0, BAZ=1 } blem;

// good
// OK enum { FOO, BAR, BAZ } blem;
// enum { FOO=0, BAR=1, BAZ=2 } blem;
// typedef enum { FOO, BAR, BAZ } blem;
// typedef enum { FOO=0, BAR=1, BAZ=2 } blem;
// typedef enum logic[1:0] { FOO, BAR, BAZ } blem;
// typedef enum logic[1:0] { FOO=0, BAR=1, BAZ=2 } blem;

// enum struct {} ? same as enum class

module Module
(
  input logic clock
);
/*public:*/

  typedef enum { A1, B1, C1 } simple_enum1;
  typedef enum { A2 = 2'b01, B2 = 8'h02, C2 = 3 } simple_enum2;

  enum { A3, B3, C3 } anon_enum_field1;
  enum { A4 = 2'b01, B4 = 8'h02, C4 = 3 } anon_enum_field2;

  typedef enum { A5, B5, C5 } enum_class1;
  typedef enum { A6 = 2'b01, B6 = 8'h02, C6 = 3 } enum_class2;

  typedef enum { FOO = 2'b01, BAR = 8'h02, BAZ = 3 } fancy_enum;

  always_comb begin /*tock*/
    simple_enum1 e1;
    simple_enum2 e2;
    enum_class1 ec1;
    enum_class2 ec2;

    e1 = A1;
    e2 = B2;
    anon_enum_field1 = C3;
    anon_enum_field2 = A4;
    ec1 = B5;
    ec2 = C6;
  end
endmodule

