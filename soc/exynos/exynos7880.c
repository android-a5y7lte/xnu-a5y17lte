/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Samsung Exynos 7880 SoC support.
 */

#include <soc/exynos7880.h>

void soc_init(void) {
	/* Allow framebuffer to be written to (same DECON trigger as 7570/7885) */
	*(volatile int*) (DECON_F_BASE + HW_SW_TRIG_CONTROL) = 0x1281;
}
