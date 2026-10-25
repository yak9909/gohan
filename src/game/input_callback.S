/* 関数途中の入口。R8/R9 以外は旗・浮動小数点を含めて元に戻す。 */
.syntax unified
.arm
.fpu vfp
.text
.align 2
.global gohan_input_pad_callback
.type gohan_input_pad_callback, %function
gohan_input_pad_callback:
    push {r0-r12, lr}
    mrs r0, cpsr
    vmrs r1, fpscr
    push {r0, r1}
    vpush {d0-d15}
    mov r0, r4
    bl gohan_input_pad
    vpop {d0-d15}
    pop {r0, r1}
    vmsr fpscr, r1
    msr cpsr_f, r0
    pop {r0-r12, lr}
    bx lr
.size gohan_input_pad_callback, .-gohan_input_pad_callback

.global gohan_input_controller_callback
.type gohan_input_controller_callback, %function
gohan_input_controller_callback:
    push {r0-r12, lr}
    mrs r0, cpsr
    vmrs r1, fpscr
    push {r0, r1}
    vpush {d0-d15}
    mov r0, r4
    /* 保存した R8/R9 を直接更新する。余分な 1 語を積まず 8 バイト境界を保つ。 */
    add r1, sp, #168
    bl gohan_input_controller
    vpop {d0-d15}
    pop {r0, r1}
    vmsr fpscr, r1
    msr cpsr_f, r0
    pop {r0-r12, lr}
    bx lr
.size gohan_input_controller_callback, .-gohan_input_controller_callback
.section .note.GNU-stack, "", %progbits
