    .syntax unified
    .arch armv6k
    .fpu vfp
    .arm
    .section .text.ShizueSkipCallback, "ax", %progbits
    .align 2
    .global ShizueSkipCallback
    .type ShizueSkipCallback, %function
ShizueSkipCallback:
    @ Preserve the interrupted game's state across the C++ callback.
    @ 14 core words + 2 status words + 16 doubles = 192 bytes; SP stays aligned.
    push    {r0-r12, lr}
    mrs     r0, cpsr
    vmrs    r1, fpscr
    push    {r0, r1}
    vpush   {d0-d15}
    bl      ShizueSkipTick
    vpop    {d0-d15}
    pop     {r0, r1}
    vmsr    fpscr, r1
    msr     cpsr_f, r0
    pop     {r0-r12, pc}
    .size ShizueSkipCallback, .-ShizueSkipCallback
    .section .note.GNU-stack,"",%progbits
