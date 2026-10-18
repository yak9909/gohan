/* 関数途中の呼び出しなので、呼び出し元の汎用・浮動小数点レジスタを保存する。 */
.syntax unified
.arm
.fpu vfp
.section .text.gohan_frame_callback, "ax", %progbits
.align 2
.global gohan_frame_callback
.type gohan_frame_callback, %function
gohan_frame_callback:
    push {r0-r12, lr}
    mrs r0, cpsr
    vmrs r1, fpscr
    push {r0, r1}
    vpush {d0-d15}
    bl gohan_dispatch_frame
    vpop {d0-d15}
    pop {r0, r1}
    vmsr fpscr, r1
    msr cpsr_f, r0
    pop {r0-r12, lr}
    bx lr
.size gohan_frame_callback, .-gohan_frame_callback
.section .note.GNU-stack, "", %progbits
