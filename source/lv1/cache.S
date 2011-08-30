#define CACHELINE_SIZE 128
#define CACHELINE_SIZE_LOG2 7

.globl dcache_flush
dcache_flush:
		/* round down address */
	li %r5, CACHELINE_SIZE - 1
	andc %r5, %r3, %r5
		/* increase size by alignment change */
	rldicl %r3, %r3, 0, 64-CACHELINE_SIZE_LOG2
	add %r4, %r4, %r3

1:
	dcbst %r0, %r5
	addic %r5, %r5, CACHELINE_SIZE /* also clears carry */
	subic. %r4, %r4, CACHELINE_SIZE
	bge 1b
	sync
	blr

.globl dcache_inv
dcache_inv:
		/* round down address */
	li %r5, CACHELINE_SIZE - 1
	andc %r5, %r3, %r5
		/* increase size by alignment change */
	rldicl %r3, %r3, 0, 64-CACHELINE_SIZE_LOG2
	add %r4, %r4, %r3

1:
	dcbf %r0, %r5
	addic %r5, %r5, CACHELINE_SIZE /* also clears carry */
	subic. %r4, %r4, CACHELINE_SIZE
	bge 1b
	sync
	blr

.globl flush_code
flush_code:
		/* round down address */
	li %r5, CACHELINE_SIZE - 1
	andc %r5, %r3, %r5
		/* increase size by alignment change */
	rldicl %r3, %r3, 0, 64-CACHELINE_SIZE_LOG2
	add %r4, %r4, %r3

1:
	dcbst %r0, %r5
	icbi %r0, %r5
	addic %r5, %r5, CACHELINE_SIZE /* also clears carry */
	subic. %r4, %r4, CACHELINE_SIZE
	bge 1b
	sync
	isync
	blr