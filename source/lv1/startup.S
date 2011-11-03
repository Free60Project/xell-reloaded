#include "xenonsprs.h"

.globl _start
_start:

b	start_from_rom
b	start_from_libxenon
b	.	// for future use
b	.
b	.
b	.
b	.
b	.

.globl start_from_rom
start_from_rom:
	bl	init_regs

		// disable interrupts (but enable vector available, gcc likes to use VMX
		// for memset)
	lis	%r13, 0x200
	mtmsrd	%r13, 1

	li	%r3, 2
	isync
	mtspr	lpcr, %r3
	isync
	li      %r3, 0x3FF
	rldicr  %r3, %r3, 32,31
	tlbiel	%r3,1
	sync
	isync

	mfspr	%r10, hid1
	li	%r11, 3
	rldimi	%r10, %r11, 58,4     // enable icache
	rldimi	%r10, %r11, 38,25    // instr. prefetch
	sync
	mtspr	hid1, %r10
	sync
	isync

	mfspr	%r10, lpcr
	li	%r11, 1
	rldimi	%r10, %r11, 1,62
	isync
	mtspr	lpcr, %r10
	isync

		// set stack
	li	%sp, 0
	oris	%sp, %sp, 0x8000
	rldicr	%sp, %sp, 32,31
	oris	%sp, %sp, 0x1e00

	mfspr	%r3, pir
	slwi	%r4, %r3, 16  // 64k stack
	sub	%sp, %sp, %r4
	subi	%sp, %sp, 0x80

	cmpwi	%r3, 0
	bne	other_threads_waiter

	lis	%r3, 0x8000
	rldicr  %r3, %r3, 32,31
	oris	%r3, %r3, start@h
	ori	%r3, %r3, start@l
	ld	%r2, 8(%r3)

	lis	%r3, 0x8000
	sldi	%r3, %r3, 32
	oris	%r3, %r3, 1f@h
	ori	%r3, %r3, 1f@l
	mtctr	%r3
	bctr
1:


	mfspr	%r3, pir
	mfspr	%r4, hrmor
	mfpvr	%r5
	bl	start

1:
	b	1b

start_from_libxenon:
	lis	%r3, wakeup_cpus@h
	ori	%r3, %r3, wakeup_cpus@l
	li	%r4, 1
	std	%r4, 0(%r3)
		
	b	start_from_rom


other_threads_waiter:
	lis	%r4, 0x8000
	rldicr	%r4, %r4, 32,31
	oris	%r4, %r4, processors_online@h
	ori	%r4, %r4, processors_online@l
	slwi	%r3, %r3, 2
	add	%r4, %r4, %r3
	li	%r5, 1
	stw	%r5, 0(%r4)

	lis	%r3, 0x8000
	rldicr	%r3, %r3, 32,31
	oris	%r3, %r3, secondary_hold_addr@h
	ori	%r3, %r3, secondary_hold_addr@l

1:
	or	%r1, %r1, %r1	// low priority
	ld	%r4, 0(%r3)
	cmpwi	%r4, 0
	beq	1b

	li	%r3, 0
	mtspr	hrmor, %r3
	mtspr	rmor, %r3

	mtctr	%r4

	mfspr	%r3, pir

	bctr

.globl other_threads_startup
other_threads_startup:
	mfspr	%r3, pir
	andi.   %r3,%r3,1
	cmplwi  %r3,1
	beq	1f

	bl	init_regs
	
	li	%r3,0
	mtspr	hrmor,%r3
	sync
	isync

	lis	%r3,0xC0
	mtspr	ctrlwr,%r3
	sync
	isync

1:
	li	%r4,0x30
	mfmsr	%r3
	andc	%r3,%r3,%r4
	mtsrr1	%r3

	lis	%r3, start_from_rom@h
	ori	%r3, %r3, start_from_rom@l

	mtsrr0	%r3
	rfid

init_regs:
	or	%r2, %r2, %r2 // normal priority

	li	%r3, 0
	mtspr	hid0, %r3
	sync
	isync

	li	%r3, 0x3f00
	rldicr	%r3, %r3, 32,31
	mtspr	hid4, %r3
	sync
	isync

	lis	%r3, 0x9c30
	ori	%r3,%r3, 0x1040
	rldicr	%r3, %r3, 32,31
	mtspr   hid1, %r3
	sync
	isync

	lis	%r3, 1
	ori	%r3,%r3, 0x8038
	rldicr	%r3, %r3, 32,31
	mtspr	hid6, %r3
	sync
	isync

	lis	%r3, 0x1d
	mtspr	tscr, %r3
	sync
	isync

	li	%r3, 0x1000
	mtspr	ttr, %r3
	sync
	isync

	blr

.globl other_threads_startup_end
other_threads_startup_end:


.globl fix_hrmor
fix_hrmor:
	li %r3, 0
	mtspr hrmor, %r3
	blr

	// r6 = addr, r7 = hrmor
.globl jump
jump:
	mtspr rmor, %r7
	mtspr hrmor, %r7
	isync
	sync
	mtsrr0 %r6

		/* switch into real mode */
	mfmsr %r6
	li %r7, 0x30
	andc %r6, %r6, %r7
	mtsrr1 %r6
	rfid
