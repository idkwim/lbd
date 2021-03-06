// ~/llvm/release/cmake_debug_build/bin/Debug/clang -target mips-unknown-linux-gnu -c ch_run_backend.cpp -emit-llvm -o ch_run_backend.bc
// /Users/Jonathan/llvm/test/cmake_debug_build/bin/Debug/llc -march=cpu0 -relocation-model=static -filetype=obj ch_run_backend.bc -o ch_run_backend.cpu0.o
// /Users/Jonathan/llvm/test/cmake_debug_build/bin/Debug/llc -march=cpu0 -mcpu=cpu032II -relocation-model=static -filetype=obj ch_run_backend.bc -o ch_run_backend.cpu0.o
// /Users/Jonathan/llvm/test/cmake_debug_build/bin/Debug/llvm-objdump -d ch_run_backend.cpu0.o | tail -n +6| awk '{print "/* " $1 " */\t" $2 " " $3 " " $4 " " $5 "\t/* " $6"\t" $7" " $8" " $9" " $10 "\t*/"}' > ../cpu0_verilog/cpu0.hex

/// start
#include "debug.h"
#include "boot.cpp"

#include "print.h"

int test_math();
int test_div();
int test_local_pointer();
int test_andorxornot();
int test_setxx();
bool test_load_bool();
int test_signed_char();
int test_unsigned_char();
int test_signed_short();
int test_unsigned_short();
long long test_longlong();
int test_control1();
int test_madd();
int test_vararg();

int main()
{
  int a = 0;
  a = test_math();
  print_integer(a);  // a = 74
  a = test_div();
  print_integer(a);  // a = 253
  a = test_local_pointer();
  print_integer(a);  // a = 3
  a = (int)test_load_bool();
  print_integer(a);  // a = 1
  a = test_andorxornot(); // a = 14
  print_integer(a);
  a = test_setxx(); // a = 3
  print_integer(a);
  a = test_signed_char();
  print_integer(a); // a = -126
  a = test_unsigned_char();
  print_integer(a); // a = 130
  a = test_signed_short();
  print_integer(a); // a = -32766
  a = test_unsigned_short();
  print_integer(a); // a = 32770
  long long b = test_longlong(); // 0x800000002
  print_integer((int)(b >> 32)); // 393307
  print_integer((int)b); // 16777222
  a = test_control1();
  print_integer(a);	// a = 51
  print_integer(2147483647); // test mod % (mult) from itoa.cpp
  print_integer(-2147483648); // test mod % (multu) from itoa.cpp
  a = test_madd();
  print_integer(a); // a = 7
  a = test_vararg();
  print_integer(a); // a = 15

  return a;
}

#include "print.cpp"

void print1_integer(int x)
{
  asm("ld $at, 8($sp)");
  asm("st $at, 28672($0)");

  return;
}

#if 0
// For instruction IO
void print2_integer(int x)
{
  asm("ld $at, 8($sp)");
  asm("outw $tat");
  return;
}
#endif

#include "ch4_1.cpp"
#include "ch4_3.cpp"
#include "ch4_5.cpp"
#include "ch7_1.cpp"
#include "ch7_2_2.cpp"
#include "ch7_3.cpp"
#include "ch7_4.cpp"
#include "ch8_1_1.cpp"
#include "ch9_1_4.cpp"
#include "ch9_3.cpp"

/* result:
74
253
3
1
14
3
-126
130
-32766
32770
393307
16777222
51
2147483647
-2147483648
7
15
RET to PC < 0, finished!
*/


