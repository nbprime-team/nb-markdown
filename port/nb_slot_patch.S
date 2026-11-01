/*
 * nb_svc.s -- nbCode's ARM layer: the firmware SVC wrappers, the cp15 sequence
 * that makes a patch to the firmware's input slot safe, and the ELF entry point.
 *
 * FIRMWARE CALLING CONVENTION (the same one DOOM, primetcc and primeLua use):
 *
 *     push {r0}
 *     push {lr}
 *     svc  <service id>
 *
 * The firmware's SVC dispatcher finds the argument block through the pushed r0,
 * calls the service with r0-r3, and returns through the pushed LR -- so to the
 * caller this looks like an ordinary AAPCS call with up to four integer
 * arguments and the result in r0.
 *
 * TARGET
 * ARM926EJ-S (ARMv5TEJ), soft float, position independent.  The image is loaded
 * by the calculator's MicroPython shellcode loader, which applies R_ARM_RELATIVE
 * relocations only, hence -fPIC everywhere and no absolute addresses in this
 * file: literals are PC-relative through ldr =sym.
 */
	.arm
	.syntax unified
	.text
nb_slot_patch:
	mov	r3, #0
	mrs	r4, cpsr
	orr	r5, r4, #0x80		/* mask IRQ: the patch is two words and an
					 * interrupt between them runs the trap
					 * half-written */
	msr	cpsr_c, r5
	mrc	p15, 0, r6, c3, c0, 0	/* save the translation table base */
	mvn	r5, #0
	mcr	p15, 0, r5, c3, c0, 0	/* quiesce (DOOM's poison) */
1:
	ldr	r5, [r1], #4
	str	r5, [r0], #4
	subs	r2, r2, #1
	bne	1b
	mcr	p15, 0, r3, c7, c10, 1	/* clean+invalidate D-cache */
	mcr	p15, 0, r3, c7, c5, 0	/* invalidate I-cache */
	mcr	p15, 0, r3, c7, c10, 4	/* drain the write buffer */
	mcr	p15, 0, r6, c3, c0, 0	/* translation table base back */
	msr	cpsr_c, r4
	bx	lr

/* the same cache maintenance for a plain data write (no quiesce: the target is
 * writable memory, not firmware code) */
	.globl nb_flush_dcache
nb_flush_dcache:
	mov	r0, #0
	mcr	p15, 0, r0, c7, c10, 1
	mcr	p15, 0, r0, c7, c5, 0
	mcr	p15, 0, r0, c7, c10, 4
	bx	lr

/* ===========================================================================
 * division helpers: -nostdlib means libgcc's __aeabi_* are linked in, but the
 * compiler also emits calls to these on this target, and the firmware's idea of
 * "divide by zero" is a reset.  Integers only (soft float throughout).
 * =========================================================================== */
	.globl __aeabi_idiv0
__aeabi_idiv0:
	mov	r0, #0
	bx	lr

	.globl __aeabi_ldiv0
__aeabi_ldiv0:
	mov	r0, #0
	mov	r1, #0
	bx	lr.size	nb_slot_patch, .-nb_slot_patch
