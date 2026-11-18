// Slingshot's co-op bridge (slingshot/src/coop/bridge.c): a call into the game's own code on the game's stack, and the memory probes the bridge
// reads and writes game memory with, which a fault ends with a failure instead of a crash (slingshot/src/coop/bridge.c's fault hook sends a fault
// between coop_probe_start and coop_probe_end to coop_probe_fault, which returns 0).
    .text

// uint64_t coop_guest_call(uint64_t fn, const uint64_t x[9], uint64_t sp, const float *s0)
// Calls fn with x0..x8 from x[] and s0 from *s0, its stack pointer sp (16-aligned; the call's stack words already at [sp]),
// and returns its x0. The host's stack pointer comes back after the call; x19..x28 and d8..d15 are the callee's to keep (AAPCS64,
// which the game's code follows), x19 holds the host sp meanwhile.
    .globl _coop_guest_call
    .p2align 2
_coop_guest_call:
    stp x29, x30, [sp, #-32]!
    mov x29, sp
    stp x19, x20, [sp, #16]
    mov x19, sp
    mov x16, x0
    mov x17, x1
    ldr s0, [x3]
    mov sp, x2
    ldp x0, x1, [x17, #0]
    ldp x2, x3, [x17, #16]
    ldp x4, x5, [x17, #32]
    ldp x6, x7, [x17, #48]
    ldr x8, [x17, #64]
    blr x16
    mov sp, x19
    ldp x19, x20, [sp, #16]
    ldp x29, x30, [sp], #32
    ret

// A wrapped game function (dm/games/0400486024DEC000/patches.py COOP_WRAPS): its first instruction branches to a stub, which comes here once
// the co-op module is on, with x16 = the wrap id and x15 = the function's own start; x0..x8 and q0..q7 are its arguments, sp the caller's
// (stack arguments above it), x30 the caller's return. coop_wrap_before (slingshot/src/coop/bridge.c) keeps the return and the id on a per-thread stack and
// calls the module (phase 0) with x0..x30 and sp, which it may change; the function then runs with the caller's sp as it was and comes
// back here, and coop_wrap_after calls the module (phase 1: x0 the result, x19..x29 the caller's again, x30 its return) and gives back the
// caller's return. Phase 0 answering 1 skips
// the function (the module set x0). The frame (0x1a0): x0..x30 and sp at +0 (the module's x[32]), q0..q7 at +256, x15 / x16 at +384
    .globl _coop_wrap_entry
    .p2align 2
_coop_wrap_entry:
    sub sp, sp, #0x1a0
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    add x9, sp, #0x1a0
    stp x30, x9, [sp, #240]
    stp q0, q1, [sp, #256]
    stp q2, q3, [sp, #288]
    stp q4, q5, [sp, #320]
    stp q6, q7, [sp, #352]
    stp x15, x16, [sp, #384]
    mov x19, sp                     // the frame (x19 is the caller's again below)
    mov x0, sp
    bl _dm_bigstack_top             // the module on this thread's big host stack (hle_bigstack.c): a game thread's own can be 8 KB
    cbz x0, 2f
    mov sp, x0
2:  ldr w0, [x19, #392]
    mov x1, x19
    bl _coop_wrap_before
    mov sp, x19
    ldr x19, [sp, #152]
    mov w9, w0
    ldp q0, q1, [sp, #256]
    ldp q2, q3, [sp, #288]
    ldp q4, q5, [sp, #320]
    ldp q6, q7, [sp, #352]
    ldr x15, [sp, #384]
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]
    ldr x8, [sp, #64]
    add sp, sp, #0x1a0
    cbnz w9, 1f
    adr x30, 1f
    br x15
1:  sub sp, sp, #0x1a0
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    add x9, sp, #0x1a0
    stp xzr, x9, [sp, #240]
    stp q0, q1, [sp, #256]
    stp q2, q3, [sp, #288]
    mov x19, sp
    mov x0, sp
    bl _dm_bigstack_top
    cbz x0, 3f
    mov sp, x0
3:  mov x0, x19
    bl _coop_wrap_after
    mov sp, x19
    ldr x19, [sp, #152]
    mov x30, x0
    ldp q0, q1, [sp, #256]
    ldp q2, q3, [sp, #288]
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]
    ldr x8, [sp, #64]
    add sp, sp, #0x1a0
    ret

// int coop_probe_copy(void *dst, const void *src, size_t n): 1, or 0 when a byte of either faulted (part may be copied by then)
    .globl _coop_probe_start
    .globl _coop_probe_copy
    .p2align 2
_coop_probe_start:
_coop_probe_copy:
    cmp x2, #8
    b.lo 3f
1:  ldr x9, [x1], #8
    str x9, [x0], #8
    sub x2, x2, #8
    cmp x2, #8
    b.hs 1b
3:  cbz x2, 2f
4:  ldrb w9, [x1], #1
    strb w9, [x0], #1
    subs x2, x2, #1
    b.ne 4b
2:  mov w0, #1
    ret

// int coop_probe_cas32(uint32_t *a, uint32_t expected, uint32_t desired, uint32_t *seen): 1 with *seen = the value that was there
// (the swap made when it was expected), 0 when a faulted
    .globl _coop_probe_cas32
    .p2align 2
_coop_probe_cas32:
    mov w9, w1
    casal w9, w2, [x0]
    str w9, [x3]
    mov w0, #1
    ret

    .globl _coop_probe_end
_coop_probe_end:
    .globl _coop_probe_fault
_coop_probe_fault:
    mov w0, #0
    ret
