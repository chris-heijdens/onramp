@x
  0
@{50_Ix0_x
  enter
  imw r0 ^x
  add r0 rpp r0
  mov r1 4
  stw r1 r0 0
  leave
  ret
=main
  enter
  mov r0 4
  imw r1 ^x
  ldw r1 rpp r1
  sub r0 r0 r1
  leave
  ret
