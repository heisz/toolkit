/*
 * x86-64 assembly context switching for the scheduler.
 *
 * Copyright (C) 2024-2026 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */

.text

/* Structure offsets, see the structural definition in scheduler.c */
#define CTX_RSP 0
#define CTX_RIP 8
#define CTX_RBP 16
#define CTX_RBX 24
#define CTX_R12 32
#define CTX_R13 40
#define CTX_R14 48
#define CTX_R15 56

/* 
 * Function: void _gmps_ctx_switch(from [rdi], to [rsi])
 */
.globl _gmps_ctx_switch
.type _gmps_ctx_switch, @function
_gmps_ctx_switch:
    /* Save callee-saved registers rbp, rbx, r12-r15 */
    MOVQ %rbp, CTX_RBP(%rdi)
    MOVQ %rbx, CTX_RBX(%rdi)
    MOVQ %r12, CTX_R12(%rdi)
    MOVQ %r13, CTX_R13(%rdi)
    MOVQ %r14, CTX_R14(%rdi)
    MOVQ %r15, CTX_R15(%rdi)

    /* Save the return address (currently at top of stack) as rip */
    MOVQ (%rsp), %rax
    MOVQ %rax, CTX_RIP(%rdi)

    /* Save stack pointer (after popping return address, so add 8) */
    LEAQ 8(%rsp), %rax
    MOVQ %rax, CTX_RSP(%rdi)

    /* --- */

    /* Restore callee-saved registers */
    MOVQ CTX_RBP(%rsi), %rbp
    MOVQ CTX_RBX(%rsi), %rbx
    MOVQ CTX_R12(%rsi), %r12
    MOVQ CTX_R13(%rsi), %r13
    MOVQ CTX_R14(%rsi), %r14
    MOVQ CTX_R15(%rsi), %r15

    /* Restore stack pointer */
    MOVQ CTX_RSP(%rsi), %rsp

    /* Branch to the saved instruction pointer */
    JMPQ *CTX_RIP(%rsi)

.size _gmps_ctx_switch, .-_gmps_ctx_switch

/*
 * Function: void _gmps_ctx_jump(ctx [rdi])
 */
.globl _gmps_ctx_jump
.type _gmps_ctx_jump, @function
_gmps_ctx_jump:
    /* Restore callee-saved registers rbp, rbx, r12-r15 */
    MOVQ CTX_RBP(%rdi), %rbp
    MOVQ CTX_RBX(%rdi), %rbx
    MOVQ CTX_R12(%rdi), %r12
    MOVQ CTX_R13(%rdi), %r13
    MOVQ CTX_R14(%rdi), %r14
    MOVQ CTX_R15(%rdi), %r15

    /* Restore stack pointer */
    MOVQ CTX_RSP(%rdi), %rsp

    /* Branch to the saved instruction pointer */
    JMPQ *CTX_RIP(%rdi)

.size _gmps_ctx_jump, .-_gmps_ctx_jump

/*
 * Function: _gmps_ctx_trampoline()
 */
.globl _gmps_ctx_trampoline
.type _gmps_ctx_trampoline, @function
_gmps_ctx_trampoline:
    CALL _gmps_ctx_trampoline_return

.size _gmps_ctx_trampoline, .-_gmps_ctx_trampoline

/* Mark stack as non-executable for security */
#if defined(__linux__) && defined(__ELF__)
.section .note.GNU-stack,"",%progbits
#endif
