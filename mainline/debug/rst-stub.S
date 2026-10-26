/*
 * EVIDENCE 29, test "rststub": a 64-byte arm64 Image header plus a few
 * instructions in place of the kernel. U-Boot's bootm does everything it does
 * for Linux (FIT, DT fixups, announce_and_cleanup) and jumps here; the stub
 * draws a white stripe (row 600), triggers DECON, waits 2 s on the counter
 * and writes PMU SWRESET (plain, INFORM3 untouched). Loops = U-Boot's
 * hand-off state is fine; hangs = the hand-off is the cause, not the kernel.
 * Build: STUB=1 MILESTONE=rststub ./build-mainline.sh
 */
	.text
	.globl _start
_start:
	b	1f			/* code0 */
	nop				/* code1 */
	.quad	0			/* text_offset */
	.quad	_end - _start		/* image_size */
	.quad	0			/* flags */
	.quad	0, 0, 0			/* res2-4 */
	.ascii	"ARM\x64"		/* magic */
	.long	0			/* res5 */
1:	ldr	x1, =0x67000000 + 600 * 4320
	mov	w3, #-1
	mov	x4, #40 * 1080
2:	str	w3, [x1], #4
	subs	x4, x4, #1
	b.ne	2b
	dsb	sy
	ldr	x1, =0x14830070
	mov	w3, #0x10
	str	w3, [x1]
	dsb	sy
	mrs	x5, cntvct_el0
	ldr	x6, =52000000
3:	mrs	x7, cntvct_el0
	sub	x7, x7, x5
	cmp	x7, x6
	b.lo	3b
	ldr	x1, =0x106b0400
	mov	w3, #1
	str	w3, [x1]
	dsb	sy
4:	wfi
	b	4b
	.ltorg
_end:
