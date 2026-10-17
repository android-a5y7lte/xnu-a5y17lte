/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Samsung Exynos 7880 register map used by uniLoader.
 *
 * Sources (universal7880 kernel, branch hybris-17.1):
 *   arch/arm64/boot/dts/exynos7880.dtsi
 *   drivers/video/fbdev/exynos/decon_7880/regs-decon.h
 *   drivers/video/fbdev/exynos/decon_7880/vpp/regs-vpp.h
 */

#ifndef EXYNOS7880_H_	/* Include guard */
#define EXYNOS7880_H_

/* Display controller (dtsi: decon_f@0x14830000) */
#define DECON_F_BASE		0x14830000
#define HW_SW_TRIG_CONTROL	0x70	/* regs-decon.h:92 */
#define DECON_WIN_CONTROL(w)	(0x130 + ((w) * 0x20))	/* regs-decon.h:177 */

/* Display DMA channels (dtsi: idma_g0..g2, idma_vg0..) */
#define IDMA_G0_BASE		0x14821000
#define IDMA_NUM_CHANNELS	6
#define IDMA_STRIDE		0x1000
#define VG_BASE_ADDR_Y(n)	(0x74 + (n) * 0x4)	/* regs-vpp.h:140 */

/* Debug UART (dtsi: serial_2 / U-Boot uart2) */
#define UART2_BASE		0x13820000

/* GIC-400 (dtsi: gic, arm,cortex-a15-gic) */
#define GICD_BASE		0x106E1000
#define GICC_BASE		0x106E2000

/* DRAM banks (exynos7880-a5y17lte_common.dtsi:21,26) */
#define DRAM0_BASE		0x40000000
#define DRAM0_SIZE		0x3E400000
#define DRAM1_BASE		0x80000000
#define DRAM1_SIZE		0x80000000

#endif // EXYNOS7880_H_
