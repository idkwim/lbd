// clang -c ch4_4.cpp -emit-llvm -o ch4_4.bc
// /Users/Jonathan/llvm/test/cmake_debug_build/bin/Debug/llc -march=cpu0 -relocation-model=pic -filetype=asm ch4_4.bc -o ch4_4.cpu0.s

/// start
int test_local_pointer()
{
  int b = 3;
  
  int* p = &b;

  return *p;
}
