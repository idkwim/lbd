; RUN: llc  -march=cpu0 -mcpu=cpu032II  -relocation-model=pic < %s | FileCheck %s
; terminal command: llc  -march=cpu0 -mcpu=cpu032II  -relocation-model=pic %s -o - | FileCheck %s

@i = global i32 1, align 4
@j = global i32 10, align 4
@k = global i32 1, align 4
@r1 = common global i32 0, align 4
@r2 = common global i32 0, align 4

define void @test() nounwind {
entry:
  %0 = load i32* @i, align 4
  %1 = load i32* @k, align 4
  %cmp = icmp eq i32 %0, %1
  %conv = zext i1 %cmp to i32
  store i32 %conv, i32* @r1, align 4

; CHECK:	xor	$[[T0:[0-9]+]], ${{[0-9]+}}, ${{[0-9]+}}
; CHECK:	sltiu	${{[0-9]+}}, $[[T0]], 1
  ret void
}

