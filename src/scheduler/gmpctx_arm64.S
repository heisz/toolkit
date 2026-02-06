/*
 * ARM64 (AArch64) assembly context switching for the scheduler.
 *
 * Copyright (C) 2024-2026 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */

.text

/* Structure offsets, see the structural definition in scheduler.c */
#define CTX_SP  0
#define CTX_PC  8
#define CTX_X29 16
#define CTX_X19 24
#define CTX_X20 32
#define CTX_X21 40
#define CTX_X22 48
#define CTX_X23 56
#define CTX_X24 64
#define CTX_X25 72
#define CTX_X26 80
#define CTX_X27 88
#define CTX_X28 96

/*
 * Function: void _gmps_ctx_switch(from [x0], to [x1])
 */
.globl _gmps_ctx_switch
.type _gmps_ctx_switch, %function
_gmps_ctx_switch:
    /* Save x30 (link register/return address) as pc */
    STR x30, [x0, #CTX_PC]

    /* Save stack pointer */
    MOV x9, sp
    STR x9, [x0, #CTX_SP]

    /* Save frame pointer */
    STR x29, [x0, #CTX_X29]

    /* Save callee-saved registers x19-x28 */
    STP x19, x20, [x0, #CTX_X19]
    STP x21, x22, [x0, #CTX_X21]
    STP x23, x24, [x0, #CTX_X23]
    STP x25, x26, [x0, #CTX_X25]
    STP x27, x28, [x0, #CTX_X27]

    /* --- */

    /* Restore callee-saved registers */
    LDP x19, x20, [x1, #CTX_X19]
    LDP x21, x22, [x1, #CTX_X21]
    LDP x23, x24, [x1, #CTX_X23]
    LDP x25, x26, [x1, #CTX_X25]
    LDP x27, x28, [x1, #CTX_X27]

    /* Restore frame pointer */
    LDR x29, [x1, #CTX_X29]

    /* Restore stack pointer */
    LDR x9, [x1, #CTX_SP]
    MOV sp, x9

    /* Load target pc into x30 and branch to it */
    LDR x30, [x1, #CTX_PC]
    RET

.size _gmps_ctx_switch, .-_gmps_ctx_switch

/*
 * Function: void _gmps_ctx_jump(ctx [x0])
 */
.globl _gmps_ctx_jump
.type _gmps_ctx_jump, %function
_gmps_ctx_jump:
    /* Restore callee-saved registers x19-x28 */
    LDP x19, x20, [x0, #CTX_X19]
    LDP x21, x22, [x0, #CTX_X21]
    LDP x23, x24, [x0, #CTX_X23]
    LDP x25, x26, [x0, #CTX_X25]
    LDP x27, x28, [x0, #CTX_X27]

    /* Restore frame pointer */
    LDR x29, [x0, #CTX_X29]

    /* Restore stack pointer */
    LDR x9, [x0, #CTX_SP]
    MOV sp, x9

    /* Load target pc into x30 and branch to it */
    LDR x30, [x0, #CTX_PC]
    RET

.size _gmps_ctx_jump, .-_gmps_ctx_jump

/*
 * Function: _gmps_ctx_trampoline()
 */
.globl _gmps_ctx_trampoline
.type _gmps_ctx_trampoline, %function
_gmps_ctx_trampoline:
    BL _gmps__ctx_trampoline_return

.size _gmps_ctx_trampoline, .-_gmps_ctx_trampoline

/* Mark stack as non-executable for security */
#if defined(__linux__) && defined(__ELF__)
.section .note.GNU-stack,"",%progbits
#endif
