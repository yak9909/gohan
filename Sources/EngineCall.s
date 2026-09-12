.syntax unified
.arm
.fpu vfp
.text
.align 2
.global SwkbdEngine_Call
.type SwkbdEngine_Call, %function
SwkbdEngine_Call:
    push {r4-r11, lr}
    sub sp, sp, #4
    vpush {d8-d15}
    vmrs r3, fpscr
    push {r3, r12}
    mov r4, sp
    mov r12, r0
    mov r5, r2
    bic sp, r1, #7
    sub sp, sp, #8
    ldr r0, [r5, #16]
    ldr r1, [r5, #20]
    str r0, [sp]
    str r1, [sp, #4]
    ldm r5, {r0-r3}
    blx r12
    mov sp, r4
    pop {r3, r12}
    vmsr fpscr, r3
    vpop {d8-d15}
    add sp, sp, #4
    pop {r4-r11, pc}
.size SwkbdEngine_Call, .-SwkbdEngine_Call

.global SwkbdEngine_SyncCode
.type SwkbdEngine_SyncCode, %function
SwkbdEngine_SyncCode:
    push {r4, lr}
    svc #0x92
    svc #0x94
    pop {r4, pc}
.size SwkbdEngine_SyncCode, .-SwkbdEngine_SyncCode
