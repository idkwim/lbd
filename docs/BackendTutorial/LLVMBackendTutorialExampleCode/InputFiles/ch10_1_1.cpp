// clang -target mips-unknown-linux-gnu -c ch10_1_1.cpp -emit-llvm -o ch10_1_1.bc
// /Users/Jonathan/llvm/test/cmake_debug_build/bin/Debug/llc -march=cpu0 -relocation-model=pic -filetype=asm ch10_1_1.bc -o ch10_1_1.cpu0.s
// /Users/Jonathan/llvm/test/cmake_debug_build/bin/Debug/llc -march=cpu0 -relocation-model=pic -filetype=obj ch10_1_1.bc -o ch10_1_1.cpu0.o

/// start
int main()
{
  int a = 5;
  int g, h, i = 0;
  
  g = (a & 0xff00);
  h = (a | 0x00ff);
  i = (a ^ 0x0ff0);

  return 0;
}

