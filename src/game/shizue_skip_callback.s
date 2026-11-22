/* 通常フックから戻るまでゲームの状態を保持する。192 B で SP の 8 B 整列を保つ。 */
.syntax unified
.arch armv6k
.fpu vfp
.arm
.section .text.gohan_shizue_skip_callback, "ax", %progbits
.align 2
.global gohan_shizue_skip_callback
.type gohan_shizue_skip_callback, %function
gohan_shizue_skip_callback:
    push {r0-r12, lr}
    mrs r0, cpsr
    vmrs r1, fpscr
    push {r0, r1}
    vpush {d0-d15}
    bl gohan_shizue_skip_tick
    vpop {d0-d15}
    pop {r0, r1}
    vmsr fpscr, r1
    msr cpsr_f, r0
    pop {r0-r12, pc}
.size gohan_shizue_skip_callback, .-gohan_shizue_skip_callback
.section .note.GNU-stack,"",%progbits
