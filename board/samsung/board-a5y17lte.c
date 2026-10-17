/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Samsung Galaxy A5 (2017) - a5y17lte - Exynos 7880
 *
 * Step 1 of the XNU port: prove that uniLoader runs after S-Boot,
 * that the boot framebuffer is where we expect, and record the CPU
 * state we are entered in (exception level, MMU/caches, timer).
 *
 * Framebuffer 0x67000000: used by S-Boot on the sibling Exynos
 * 7570/7580/7870 phones (uniLoader j4lte/a3xelte, mainline
 * exynos7870-j5y17lte and U-Boot on7xelte, the latter 1080x1920).
 * The IDMA base-address registers are printed so the real address
 * can be confirmed on this device.
 */
#include <board.h>
#include <util.h>
#include <stdint.h>
#include <drivers/framework.h>
#include <lib/simplefb.h>
#include <lib/debug.h>
#include <lib/console.h>
#include <soc/exynos7880.h>

#define A5Y17LTE_FB_BASE	0x67000000
#define A5Y17LTE_FB_WIDTH	1080
#define A5Y17LTE_FB_HEIGHT	1920

#define READ_SYSREG(reg) ({ \
	uint64_t __v; \
	__asm__ volatile("mrs %0, " #reg : "=r"(__v)); \
	__v; })

static inline uint32_t rd32(uintptr_t addr)
{
	return *(volatile uint32_t *)addr;
}

static void print_reg64(const char *name, uint64_t v)
{
	printk(KERN_INFO, "%s = 0x%08x%08x\n", name,
	       (unsigned int)(v >> 32), (unsigned int)(v & 0xffffffff));
}

static inline void wr32(uintptr_t addr, uint32_t v)
{
	*(volatile uint32_t *)addr = v;
}

static uint64_t timer_freq(void)
{
	uint64_t f = READ_SYSREG(CNTFRQ_EL0);
	return f ? f : 26000000;	/* dtsi: arm,armv8-timer 26 MHz */
}

static void delay_ms(uint64_t ms)
{
	uint64_t start = READ_SYSREG(CNTPCT_EL0);
	uint64_t ticks = timer_freq() / 1000 * ms;

	while (READ_SYSREG(CNTPCT_EL0) - start < ticks)
		;
}

/*
 * Push the framebuffer to the command-mode panel: one software-
 * triggered frame. Without this the panel keeps showing the S-Boot
 * logo from its own memory (step 1 hardware result, 2026-10-18).
 */
static void decon_kick(void)
{
	__asm__ volatile("dsb sy" ::: "memory");
	wr32(DECON_F_BASE + HW_SW_TRIG_CONTROL, DECON_SW_TRIG_IDLE);
	wr32(DECON_F_BASE + HW_SW_TRIG_CONTROL, DECON_SW_TRIG_FIRE);
	delay_ms(20);	/* one 60 Hz frame is ~16.7 ms */
}

int a5y17lte_init(void)
{
	/* Switch DECON to software trigger so we decide when frames go out */
	wr32(DECON_F_BASE + HW_SW_TRIG_CONTROL, DECON_SW_TRIG_IDLE);
	return 0;
}

static struct video_info a5y17lte_fb = {
	.format = FB_FORMAT_ARGB8888,
	.width = A5Y17LTE_FB_WIDTH,
	.height = A5Y17LTE_FB_HEIGHT,
	.stride = 4,
	.address = (void *)A5Y17LTE_FB_BASE
};

static const struct device a5y17lte_devices[] = {
	{ "simplefb", &a5y17lte_fb, "fb" },
};

#ifdef CONFIG_A5Y17LTE_DIAG_HALT
/*
 * Four bars along the bottom: red, green, blue, white (left to right).
 * If the colours appear in a different order the pixel format is not
 * ARGB8888 and the board file must be changed.
 */
static void draw_colour_bars(void)
{
	static const uint32_t bars[4] = {
		0xffff0000, 0xff00ff00, 0xff0000ff, 0xffffffff
	};
	volatile uint32_t *fb = (volatile uint32_t *)A5Y17LTE_FB_BASE;
	int bar_w = A5Y17LTE_FB_WIDTH / 4;

	for (int y = A5Y17LTE_FB_HEIGHT - 200; y < A5Y17LTE_FB_HEIGHT; y++)
		for (int x = 0; x < A5Y17LTE_FB_WIDTH; x++)
			fb[y * A5Y17LTE_FB_WIDTH + x] = bars[x / bar_w];
}

/* Runs after the framebuffer console for every printk line */
static void kick_console_write(int level, const char *prefix, const char *msg)
{
	(void)level; (void)prefix; (void)msg;
	decon_kick();
}

static const struct console kick_console = {
	.write = kick_console_write,
};

#define A5Y17LTE_RESET_AFTER_S	30

static void pmu_reset(void)
{
	__asm__ volatile("dsb sy" ::: "memory");
	wr32(PMU_BASE + PMU_SWRESET, 1);
	for (;;)
		__asm__ volatile("wfe");
}

static int a5y17lte_diag(void)
{
	uint64_t el = (READ_SYSREG(CurrentEL) >> 2) & 3;

	console_register(&kick_console);
	decon_kick();	/* show the splash printed before late_init */

	printk(KERN_NOTICE, "=== a5y17lte step 1 diagnostics ===\n");
	printk(KERN_INFO, "CurrentEL = EL%d\n", (int)el);
	print_reg64("MIDR_EL1  ", READ_SYSREG(MIDR_EL1));
	print_reg64("MPIDR_EL1 ", READ_SYSREG(MPIDR_EL1));
	print_reg64("CNTFRQ_EL0", READ_SYSREG(CNTFRQ_EL0));
	print_reg64("DAIF      ", READ_SYSREG(DAIF));

	if (el == 1) {
		print_reg64("SCTLR_EL1 ", READ_SYSREG(SCTLR_EL1));
		print_reg64("TCR_EL1   ", READ_SYSREG(TCR_EL1));
		print_reg64("VBAR_EL1  ", READ_SYSREG(VBAR_EL1));
	} else if (el == 2) {
		print_reg64("SCTLR_EL2 ", READ_SYSREG(SCTLR_EL2));
		print_reg64("HCR_EL2   ", READ_SYSREG(HCR_EL2));
	}

	print_reg64("ID_AA64PFR0", READ_SYSREG(ID_AA64PFR0_EL1));
	print_reg64("ID_AA64MMFR0", READ_SYSREG(ID_AA64MMFR0_EL1));

	printk(KERN_INFO, "fb @ 0x%08x %dx%d (bars: R G B W)\n",
	       A5Y17LTE_FB_BASE, A5Y17LTE_FB_WIDTH, A5Y17LTE_FB_HEIGHT);
	draw_colour_bars();
	decon_kick();

	printk(KERN_INFO, "DECON TRIG = 0x%08x\n",
	       rd32(DECON_F_BASE + HW_SW_TRIG_CONTROL));
	for (int w = 0; w < 4; w++)
		printk(KERN_INFO, "DECON WIN%d = 0x%08x\n", w,
		       rd32(DECON_F_BASE + DECON_WIN_CONTROL(w)));

	/* A hang after one of these lines means that IDMA block is gated. */
	for (int ch = 0; ch < IDMA_NUM_CHANNELS; ch++) {
		uintptr_t base = IDMA_G0_BASE + ch * IDMA_STRIDE;
		printk(KERN_DEBUG, "reading IDMA%d...\n", ch);
		printk(KERN_INFO, "IDMA%d ADDR_Y0 = 0x%08x\n", ch,
		       rd32(base + VG_BASE_ADDR_Y(0)));
	}

	printk(KERN_NOTICE, "=== step 1b complete ===\n");
	printk(KERN_NOTICE, "Auto reboot in %d s (proof the code ran)\n",
	       A5Y17LTE_RESET_AFTER_S);

	for (int i = 0; i < A5Y17LTE_RESET_AFTER_S * 10; i++) {
		decon_kick();
		delay_ms(80);
	}

	pmu_reset();
	return 0;
}
#endif

struct board_data board_ops = {
	.name = "samsung-a5y17lte",
	.ops = {
		.early_init = a5y17lte_init,
#ifdef CONFIG_A5Y17LTE_DIAG_HALT
		.late_init = a5y17lte_diag,
#endif
	},
	.devices = a5y17lte_devices,
	.num_devices = ARRAY_SIZE(a5y17lte_devices),
	.quirks = 0
};
