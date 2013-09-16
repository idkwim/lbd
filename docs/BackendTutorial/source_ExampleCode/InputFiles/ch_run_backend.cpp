// clang -target `llvm-config --host-target` -c ch_run_backend.cpp -emit-llvm -o ch_run_backend.bc
// /Users/Jonathan/llvm/test/cmake_debug_build/bin/Debug/llc -march=cpu0 -relocation-model=static -filetype=obj ch_run_backend.bc -o ch_run_backend.cpu0.o
// /Users/Jonathan/llvm/test/cmake_debug_build/bin/Debug/llvm-objdump -d ch_run_backend.cpu0.o | tail -n +6| awk '{print "/* " $1 " */\t" $2 " " $3 " " $4 " " $5 "\t/* " $6"\t" $7" " $8" " $9" " $10 "\t*/"}' > ../cpu0_verilog/raw/cpu0s.hex

// /Users/Jonathan/llvm/test/cmake_debug_build/bin/Debug/llvm-objdump -d ch_run_backend.cpu0.o | tail -n +6| awk '{print "/* " $1 " */\t" $2 " " $3 " " $4 " " $5 "\t/* " $6"\t" $7" " $8" " $9" " $10 "\t*/"}' > ../cpu0_verilog/redesign/cpu0s.hex

/// start
#include "boot.cpp"

#include "print.h"

bool test_load_bool();
int test_operators(int x);
int test_control();

int main()
{
  int a = 0;
  a = (int)test_load_bool();
  print_integer(a);  // a = 1
  a = test_operators(12); // a = 13
  print_integer(a);
  a += test_control();	// a = (128+18) = 146
  print_integer(a);
  print_integer(2147483647); // test mult from itoa.cpp
  print_integer(-2147483648); // test multu from itoa.cpp

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

bool test_load_bool()
{
  int a = 1;

  if (a < 0)
    return false;

  return true;
}

int test_operators(int x)
{
  int a = 11;
  int b = 2;
  int c, d, e, f, g, h, i, j, k, l, m, n, o;
  unsigned int a1 = -11, k1 = 0;

  k = (a >> 2);
  print_integer(k); // 2
  k1 = (a1 >> 2);
  print_integer((int)k1); // 0x3fffffd = 1073741821
  c = a + b;
  d = a - b;
  e = a * b;
  f = a / b;
  g = (a & b);
  h = (a | b);
  i = (a ^ b);
  j = (a << 2);
  l = a % x;
  m = (a+1)%12;

  n = !a;
  print_integer(n); // 0
  int* p = &b;
  o = *p;
  
  return (c+d+e+f+g+h+i+j+l+m+o); // (13+9+22+5+2+11+9+44+11+0+2)=128
}

int test_control()
{
  int b = 1;
  int c = 2;
  int d = 3;
  int e = 4;
  int f = 5;
  
  if (b != 0) {
    b++;
  }
  if (c > 0) {
    c++;
  }
  if (d >= 0) {
    d++;
  }
  if (e < 0) {
    e++;
  }
  if (f <= 0) {
    f++;
  }
  
  return (b+c+d+e+f); // (2+3+4+4+5)=18
}
