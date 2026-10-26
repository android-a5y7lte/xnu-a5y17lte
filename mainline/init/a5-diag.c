// SPDX-License-Identifier: GPL-2.0
/*
 * a5-diag: hardware helper for the Galaxy A5 (2017) mainline test image.
 * Static aarch64 binary (musl). Called from /init:
 *
 *   a5-diag panel   DECON to hardware (TE) refresh, proven in M1
 *   a5-diag info    kernel, CPUs, memory, eMMC (locked read-only first)
 *   a5-diag system  SYSTEM partition: number, size, ext4 superblock
 *   a5-diag usb     USB clock/PHY state as S-Boot left it; turns on the
 *                   USB PLL and CCORE gates if they are off (M4 diagnosis)
 *   a5-diag muic    read the S2MU004 MUIC over /dev/i2c-N and, if the USB
 *                   D+/D- switch is not on USB, set manual mode + USB
 *   a5-diag usbregs DWC3 and USB PHY register dump, UDC state
 *   a5-diag android W0: simpledrm KMS device, binderfs + a binder device
 *                   (BINDER_VERSION), PSI - what Waydroid needs
 *   a5-diag held MS which volume key is held at any time within MS ms:
 *                   exit 10 Volume Down (rescue), 11 Volume Up (Nura in RAM),
 *                   1 neither
 *   a5-diag lock-except NAME
 *                   M5e: every eMMC block device read-only except the GPT
 *                   partition NAME, checked via each partition's "ro"
 *   a5-diag reboot CMD
 *                   reboot(RESTART2, CMD) for CMD = download, recovery or
 *                   bootloader (busybox reboot cannot pass a command)
 *   a5-diag keys    PID 1 loop: print key presses, hold Power 3 s ->
 *                   reboot into recovery (TWRP), reap orphaned children
 *
 * Register sources: Samsung universal7880 kernel (hybris-17.1)
 *   drivers/soc/samsung/pwrcal/S5E7880/S5E7880-cmusfr.h, -cmu.c, -pmusfr.h
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/reboot.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <linux/input.h>
#include <linux/reboot.h>
#include <linux/android/binder.h>
#include <linux/android/binderfs.h>
#include <drm/drm.h>
#include <sys/mount.h>
#include <sys/stat.h>

#define DECON_BASE		0x14830000UL
#define HW_SW_TRIG_CONTROL	0x70
#define TRIG_HW_CONTINUOUS	0x10

#define CMU_CCORE		0x10680000UL
#define CCORE_BUS_PLL_CON0	0x140
#define CCORE_MEDIA_PLL_CON0	0x120
#define CCORE_MUX_FSYS_BUS	0x284
#define CCORE_MUX_USB20DRD	0x29c
#define CCORE_DIV_FSYS_BUS	0x484
#define CCORE_DIV_USB20DRD	0x49c
#define CCORE_EN_FSYS_BUS	0x884
#define CCORE_EN_USB20DRD	0x89c

#define CMU_FSYS		0x13730000UL
#define FSYS_USB_PLL_LOCK	0x000
#define FSYS_USB_PLL_CON0	0x100
#define FSYS_MUX_USB_PLL	0x200
#define FSYS_MUX_PHYCLK_USER	0x230
#define FSYS_EN_BUS		0x804
#define FSYS_EN_USB20DRD_REF	0x828

/* K8: MMC1 (SDIO, Wi-Fi). [P] S5E7880-cmusfr.h / -cmu.c; U-Boot's boot.cmd sets these */
#define CCORE_MUX_MMC0		0x288
#define CCORE_DIV_MMC0		0x488
#define CCORE_EN_MMC0		0x888
#define CCORE_MUX_MMC1		0x28c
#define CCORE_DIV_MMC1		0x48c
#define CCORE_EN_MMC1		0x88c
#define MUX_SEL_MEDIA		(1u << 12)	/* 0: BUS_PLL/2, 1: MEDIA_PLL/2 */
#define MUX_GATE		(1u << 21)
#define DIV_RATIO_MASK		0x3ffu		/* ratio - 1 */
#define FSYS_EN_OSCCLK		0x800		/* bit 3: MMC1 OSCCLK */
#define FSYS_EN_BUS_REG		0x804		/* bit 9: MMC1 ACLK, bit 8: MMC0 */
#define FSYS_EN_MMC1		0x818		/* bit 0: MMC1 SDCLKIN */

/* K9: Bluetooth UART (UART_BTWIFIFM). [P] S5E7880-cmusfr.h; U-Boot sets these */
#define CCORE_MUX_UART_BT	0x2a4
#define CCORE_DIV_UART_BT	0x4a4
#define CCORE_EN_UART_BT	0x8a4
#define CMU_PERI		0x101f0000UL
#define PERI_EN_BUS1		0x814		/* bit 11: UART_BTWIFIFM PCLK */
#define PERI_EN_UART_BT		0x830		/* bit 0: EXT_UCLK */

#define PMU_BASE		0x106b0000UL
#define PMU_USBDEV_PHY_CONTROL	0x704

#define PLL_ENABLE		(1u << 31)
#define PLL_LOCKED		(1u << 29)
/* Samsung S5E7880-cmu.c usb_pll_rates: 50 MHz = 26 MHz * 400 / (13 << 4) */
#define USB_PLL_50MHZ		((400u << 16) | (13u << 8) | 4u)

static volatile uint32_t *map(unsigned long phys)
{
	int fd = open("/dev/mem", O_RDWR | O_SYNC);
	void *p;

	if (fd < 0)
		return NULL;
	p = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, phys);
	close(fd);
	return p == MAP_FAILED ? NULL : p;
}

#define R(b, o)		((b)[(o) / 4])

static void read_file(const char *path, char *buf, size_t len)
{
	FILE *f = fopen(path, "r");
	size_t n = 0;

	buf[0] = 0;
	if (f) {
		n = fread(buf, 1, len - 1, f);
		fclose(f);
	}
	buf[n] = 0;
	while (n && (buf[n - 1] == '\n' || buf[n - 1] == ' '))
		buf[--n] = 0;
}

static double pll141xx_mhz(uint32_t con0)
{
	unsigned int m = (con0 >> 16) & 0x3ff, p = (con0 >> 8) & 0x3f, s = con0 & 7;

	return p ? 26.0 * m / (p << s) : 0;
}

static int cmd_panel(void)
{
	volatile uint32_t *d = map(DECON_BASE);

	if (!d)
		return 1;
	R(d, HW_SW_TRIG_CONTROL) = TRIG_HW_CONTINUOUS;
	return 0;
}

static int cmd_info(void)
{
	char rel[128], cpus[64], mem[4096], name[64], size[32];
	unsigned long long sectors;
	char *p;
	int fd, ro = 1, parts = 0, rw = 0;

	read_file("/proc/sys/kernel/osrelease", rel, sizeof(rel));
	read_file("/sys/devices/system/cpu/online", cpus, sizeof(cpus));
	read_file("/proc/meminfo", mem, sizeof(mem));
	p = strchr(mem, '\n');
	if (p)
		*p = 0;
	printf(" kernel : %s aarch64\n cpus   : %s\n memory : %s\n", rel, cpus, mem);

	for (int i = 0; i < 30 && (fd = open("/dev/mmcblk0", O_RDONLY)) < 0; i++)
		usleep(200000);
	if (fd < 0) {
		printf(" emmc   : NOT FOUND\n");
		return 0;
	}
	/*
	 * Nothing in this image writes the eMMC. BLKROSET only marks the
	 * whole-disk device; force_ro (set_disk_ro) also covers every
	 * partition, which each partition's "ro" attribute then reports.
	 */
	ioctl(fd, BLKROSET, &ro);
	close(fd);
	fd = open("/sys/block/mmcblk0/force_ro", O_WRONLY);
	if (fd >= 0) {
		write(fd, "1", 1);
		close(fd);
	}
	read_file("/sys/block/mmcblk0/device/name", name, sizeof(name));
	read_file("/sys/block/mmcblk0/size", size, sizeof(size));
	sectors = strtoull(size, NULL, 10);
	for (int i = 1; i < 64; i++) {
		char path[64], val[8];

		snprintf(path, sizeof(path), "/sys/class/block/mmcblk0p%d/ro", i);
		if (access(path, F_OK))
			continue;
		parts++;
		read_file(path, val, sizeof(val));
		if (strcmp(val, "1"))
			rw++;
	}
	printf(" emmc   : %s, %llu MiB, %d partitions, %s\n", name,
	       sectors / 2048, parts, rw ? "SOME WRITABLE" : "all READ-ONLY");
	return 0;
}

/* Find a GPT partition by name: "SYSTEM" -> 19, or -1 */
static int find_part(const char *want)
{
	for (int i = 1; i < 64; i++) {
		char path[64], ue[512], *p;

		snprintf(path, sizeof(path), "/sys/class/block/mmcblk0p%d/uevent", i);
		read_file(path, ue, sizeof(ue));
		p = strstr(ue, "PARTNAME=");
		if (p && !strncmp(p + 9, want, strlen(want)) &&
		    (p[9 + strlen(want)] == '\n' || !p[9 + strlen(want)]))
			return i;
	}
	return -1;
}

/* M5: describe the SYSTEM partition (read-only: superblock only) */
static int cmd_system(void)
{
	char path[64], size[32], val[8];
	unsigned char sb[1024];
	int n = find_part("SYSTEM"), fd;

	if (n < 0) {
		printf(" system : no SYSTEM partition\n");
		return 1;
	}
	snprintf(path, sizeof(path), "/sys/class/block/mmcblk0p%d/size", n);
	read_file(path, size, sizeof(size));
	snprintf(path, sizeof(path), "/sys/class/block/mmcblk0p%d/ro", n);
	read_file(path, val, sizeof(val));
	printf(" system : mmcblk0p%d, %s sectors = %llu MiB, ro=%s\n", n, size,
	       strtoull(size, NULL, 10) / 2048, val);

	snprintf(path, sizeof(path), "/dev/mmcblk0p%d", n);
	fd = open(path, O_RDONLY);
	if (fd < 0 || pread(fd, sb, sizeof(sb), 1024) != sizeof(sb)) {
		printf(" system : superblock read FAILED\n");
		return 1;
	}
	close(fd);
	if (sb[0x38] == 0x53 && sb[0x39] == 0xef) {	/* s_magic */
		unsigned int bs = 1024u << (sb[0x18] | sb[0x19] << 8);	/* s_log_block_size */
		unsigned long long blocks = sb[4] | sb[5] << 8 | sb[6] << 16 |
					    (unsigned long long)sb[7] << 24;

		printf(" system : ext4, label \"%.16s\", %llu MiB filesystem\n",
		       (char *)sb + 0x78, blocks * bs >> 20);
	} else {
		printf(" system : not ext4 (bytes 1080: %02x %02x)\n", sb[0x38], sb[0x39]);
	}
	return 0;
}

/*
 * K8: report the MMC1 (SDIO) clock chain that U-Boot's boot.cmd set up, with
 * MMC0 (the eMMC, set by S-Boot) for comparison. If anything is still off
 * (an older U-Boot script), set it the same way and return 10 so the caller
 * re-binds the mmc1 driver; 1 on error.
 */
static double mmc_ciu_mhz(volatile uint32_t *cc, unsigned int mux, unsigned int div)
{
	double parent = (mux & MUX_SEL_MEDIA) ? pll141xx_mhz(R(cc, CCORE_MEDIA_PLL_CON0)) / 2 :
				pll141xx_mhz(R(cc, CCORE_BUS_PLL_CON0)) / 2;

	return parent / ((div & DIV_RATIO_MASK) + 1);
}

static int cmd_mmc1(void)
{
	volatile uint32_t *cc = map(CMU_CCORE), *fs = map(CMU_FSYS);
	unsigned int mux, div, en;
	int changed = 0;

	if (!cc || !fs) {
		printf(" mmc1   : /dev/mem map failed\n");
		return 1;
	}
	for (int i = 0; i < 2; i++) {
		mux = R(cc, i ? CCORE_MUX_MMC1 : CCORE_MUX_MMC0);
		div = R(cc, i ? CCORE_DIV_MMC1 : CCORE_DIV_MMC0);
		en = R(cc, i ? CCORE_EN_MMC1 : CCORE_EN_MMC0);
		printf(" mmc%d   : ccore mux %08x div %08x en %08x = CIU %.3f MHz%s%s\n", i,
		       mux, div, en, mmc_ciu_mhz(cc, mux, div),
		       (mux & MUX_GATE) ? "" : " MUXGATE-OFF", (en & 1) ? "" : " GATED");
	}
	printf(" mmc1   : fsys osc %08x bus %08x sdclk %08x%s\n", R(fs, FSYS_EN_OSCCLK),
	       R(fs, FSYS_EN_BUS_REG), R(fs, FSYS_EN_MMC1),
	       ((R(fs, FSYS_EN_BUS_REG) >> 9) & 1) && (R(fs, FSYS_EN_MMC1) & 1) ? "" : "  GATED");

	mux = R(cc, CCORE_MUX_MMC1);
	if ((mux & MUX_SEL_MEDIA) || !(mux & MUX_GATE)) {
		R(cc, CCORE_MUX_MMC1) = (mux & ~MUX_SEL_MEDIA) | MUX_GATE;
		changed = 1;
	}
	div = R(cc, CCORE_DIV_MMC1);
	if ((div & DIV_RATIO_MASK) != 3) {
		R(cc, CCORE_DIV_MMC1) = (div & ~DIV_RATIO_MASK) | 3;	/* 1599/2/4 */
		changed = 1;
	}
	if (!(R(cc, CCORE_EN_MMC1) & 1)) {
		R(cc, CCORE_EN_MMC1) |= 1;
		changed = 1;
	}
	if (!(R(fs, FSYS_EN_BUS_REG) & (1u << 9)) || !(R(fs, FSYS_EN_OSCCLK) & (1u << 3)) ||
	    !(R(fs, FSYS_EN_MMC1) & 1)) {
		R(fs, FSYS_EN_BUS_REG) |= 1u << 9;
		R(fs, FSYS_EN_OSCCLK) |= 1u << 3;
		R(fs, FSYS_EN_MMC1) |= 1;
		changed = 1;
	}
	if (changed)
		printf(" mmc1   : was not set up by U-Boot; set now: CIU %.3f MHz\n",
		       mmc_ciu_mhz(cc, R(cc, CCORE_MUX_MMC1), R(cc, CCORE_DIV_MMC1)));
	return changed ? 10 : 0;
}

/*
 * K5: Mali-T830 bring-up, in explicit steps because touching a register of
 * an unpowered or unclocked block can hang the bus (then: Volume Down +
 * Power to force off). Each step is separate:
 *   gpu status  PMU domain status only (run at every boot)
 *   gpu         read-only: also CMU_G3D, but only if the domain is powered
 *   gpu on      power the pd_g3d domain on (PMU), wait for status, dump CMU_G3D
 *   gpu clk     set the CMU_G3D gate enable bits, dump again
 *   gpu id      read GPU_ID (needs domain on and CLK_G3D gate on)
 */
#define PMU_G3D_BASE		0x106b4000UL
#define CMU_G3D			0x11460000UL
#define MALI_BASE		0x11400000UL
#define G3D_PWR_MASK		0xfu

static void gpu_dump_cmu(volatile uint32_t *g)
{
	printf(" gpu    : G3D_PLL %08x  mux pll %08x switch %08x g3d %08x\n",
	       R(g, 0x100), R(g, 0x200), R(g, 0x204), R(g, 0x208));
	printf(" gpu    : stat pll %08x switch %08x g3d %08x  div bus %08x apb %08x\n",
	       R(g, 0x600), R(g, 0x604), R(g, 0x608), R(g, 0x400), R(g, 0x404));
	printf(" gpu    : gates osc %08x bus %08x apb %08x\n",
	       R(g, 0x800), R(g, 0x804), R(g, 0x808));
}

static int cmd_gpu(const char *step)
{
	volatile uint32_t *pd = map(PMU_G3D_BASE), *g;
	uint32_t cfg, st;

	if (!pd) {
		printf(" gpu    : /dev/mem map failed\n");
		return 1;
	}
	cfg = R(pd, 0);
	st = R(pd, 4);
	printf(" gpu    : pd_g3d config %08x status %08x -> %s\n", cfg, st,
	       (st & G3D_PWR_MASK) == G3D_PWR_MASK ? "POWERED" :
	       (st & G3D_PWR_MASK) == 0 ? "off" : "partial");

	if (step && !strcmp(step, "on") && (st & G3D_PWR_MASK) != G3D_PWR_MASK) {
		R(pd, 0) = G3D_PWR_MASK;
		for (int i = 0; i < 100 && (R(pd, 4) & G3D_PWR_MASK) != G3D_PWR_MASK; i++)
			usleep(100);
		st = R(pd, 4);
		printf(" gpu    : power-on requested, status now %08x (%s)\n", st,
		       (st & G3D_PWR_MASK) == G3D_PWR_MASK ? "POWERED" : "FAILED");
		if ((st & G3D_PWR_MASK) != G3D_PWR_MASK)
			return 1;
	}
	if (step && !strcmp(step, "status"))
		return 0;	/* PMU only: safe at every boot */
	if ((st & G3D_PWR_MASK) != G3D_PWR_MASK) {
		printf(" gpu    : domain not powered, CMU_G3D and the GPU are not touched\n");
		return 0;
	}

	g = map(CMU_G3D);
	if (!g) {
		printf(" gpu    : /dev/mem map failed\n");
		return 1;
	}
	if (step && !strcmp(step, "clk")) {
		R(g, 0x800) |= 0x3;
		R(g, 0x804) |= 0x1ff;
		R(g, 0x808) |= 0x7f;
		printf(" gpu    : gate enable bits set\n");
	}
	gpu_dump_cmu(g);

	if (step && !strcmp(step, "id")) {
		volatile uint32_t *m;

		if (!(R(g, 0x804) & (1u << 6)) || !(R(g, 0x808) & 1u)) {
			printf(" gpu    : CLK_G3D / APB gate off, run `a5-diag gpu clk` first\n");
			return 1;
		}
		m = map(MALI_BASE);
		if (!m) {
			printf(" gpu    : /dev/mem map failed\n");
			return 1;
		}
		printf(" gpu    : GPU_ID %08x (Mali-T830 = 0x0830xxxx)\n", R(m, 0));
	}
	return 0;
}

/*
 * K9: report the Bluetooth UART clock chain set by U-Boot's boot.cmd (baud
 * clock 49.97 MHz); set it the same way if it is off and return 10.
 */
static int cmd_bt(void)
{
	volatile uint32_t *cc = map(CMU_CCORE), *pe = map(CMU_PERI);
	unsigned int mux, div, en;
	double mhz;
	int changed = 0;

	if (!cc || !pe) {
		printf(" bt     : /dev/mem map failed\n");
		return 1;
	}
	mux = R(cc, CCORE_MUX_UART_BT);
	div = R(cc, CCORE_DIV_UART_BT);
	en = R(cc, CCORE_EN_UART_BT);
	mhz = ((mux & MUX_SEL_MEDIA) ? pll141xx_mhz(R(cc, CCORE_MEDIA_PLL_CON0)) :
	       pll141xx_mhz(R(cc, CCORE_BUS_PLL_CON0))) / 2 / ((div & 0xf) + 1);
	printf(" bt     : uart ccore mux %08x div %08x en %08x = %.3f MHz%s%s\n", mux, div, en,
	       mhz, (mux & MUX_GATE) ? "" : " MUXGATE-OFF", (en & 1) ? "" : " GATED");
	printf(" bt     : peri bus1 %08x uart %08x%s\n", R(pe, PERI_EN_BUS1), R(pe, PERI_EN_UART_BT),
	       ((R(pe, PERI_EN_BUS1) >> 11) & 1) && (R(pe, PERI_EN_UART_BT) & 1) ? "" : "  GATED");
	if ((mux & MUX_SEL_MEDIA) || !(mux & MUX_GATE)) {
		R(cc, CCORE_MUX_UART_BT) = (mux & ~MUX_SEL_MEDIA) | MUX_GATE;
		changed = 1;
	}
	if ((div & 0xf) != 0xf) {
		R(cc, CCORE_DIV_UART_BT) = div | 0xf;	/* 799.5 / 16 */
		changed = 1;
	}
	if (!(en & 1)) {
		R(cc, CCORE_EN_UART_BT) = en | 1;
		changed = 1;
	}
	if (!(R(pe, PERI_EN_BUS1) & (1u << 11)) || !(R(pe, PERI_EN_UART_BT) & 1)) {
		R(pe, PERI_EN_BUS1) |= 1u << 11;
		R(pe, PERI_EN_UART_BT) |= 1;
		changed = 1;
	}
	if (changed)
		printf(" bt     : was not set up by U-Boot; set now\n");
	return changed ? 10 : 0;
}

static int cmd_usb(void)
{
	volatile uint32_t *cc = map(CMU_CCORE), *fs = map(CMU_FSYS), *pmu = map(PMU_BASE);
	uint32_t bus, media, pll, pll_new;
	double busmhz;

	if (!cc || !fs || !pmu) {
		printf(" usb    : /dev/mem map failed\n");
		return 1;
	}
	bus = R(cc, CCORE_BUS_PLL_CON0);
	media = R(cc, CCORE_MEDIA_PLL_CON0);
	busmhz = pll141xx_mhz(bus);

	for (int i = 0; i < 2; i++) {
		unsigned int mux = R(cc, i ? CCORE_MUX_USB20DRD : CCORE_MUX_FSYS_BUS);
		unsigned int div = R(cc, i ? CCORE_DIV_USB20DRD : CCORE_DIV_FSYS_BUS);
		unsigned int en = R(cc, i ? CCORE_EN_USB20DRD : CCORE_EN_FSYS_BUS);
		double parent = ((mux >> 12) & 1) ? pll141xx_mhz(media) / 2 : busmhz / 2;

		printf(" ccore  : %-8s mux %08x div %08x en %08x = %.3f MHz%s\n",
		       i ? "USB20DRD" : "FSYS_BUS", mux, div, en,
		       parent / ((div & 0xf) + 1), (en & 1) ? "" : "  GATED");
		if (!(en & 1)) {
			R(cc, i ? CCORE_EN_USB20DRD : CCORE_EN_FSYS_BUS) = en | 1;
			printf(" ccore  : %s gate turned ON\n", i ? "USB20DRD" : "FSYS_BUS");
		}
	}

	pll = R(fs, FSYS_USB_PLL_CON0);
	printf(" fsys   : USB_PLL %08x = %.3f MHz %s%s\n", pll, pll141xx_mhz(pll),
	       (pll & PLL_ENABLE) ? "on" : "OFF", (pll & PLL_LOCKED) ? " locked" : "");
	if (!(pll & PLL_ENABLE)) {
		pll_new = (pll & ~0x03ff3f07u) | USB_PLL_50MHZ;
		R(fs, FSYS_USB_PLL_LOCK) = 13 * 200;	/* >= 200 x PDIV cycles */
		R(fs, FSYS_USB_PLL_CON0) = pll_new;
		R(fs, FSYS_USB_PLL_CON0) = pll_new | PLL_ENABLE;
		for (int i = 0; i < 1000 && !(R(fs, FSYS_USB_PLL_CON0) & PLL_LOCKED); i++)
			usleep(10);
		pll = R(fs, FSYS_USB_PLL_CON0);
		printf(" fsys   : USB_PLL set to %08x = %.3f MHz %s\n", pll,
		       pll141xx_mhz(pll), (pll & PLL_LOCKED) ? "locked" : "NOT LOCKED");
	}
	printf(" fsys   : mux_usb_pll %08x phyclk_user %08x en_bus %08x en_ref %08x\n",
	       R(fs, FSYS_MUX_USB_PLL), R(fs, FSYS_MUX_PHYCLK_USER),
	       R(fs, FSYS_EN_BUS), R(fs, FSYS_EN_USB20DRD_REF));
	printf(" pmu    : USBDEV_PHY_CONTROL %08x\n", R(pmu, PMU_USBDEV_PHY_CONTROL));
	return 0;
}

/* ---- S2MU004 MUIC (Samsung include/linux/mfd/samsung/s2mu004-private.h,
 *      include/linux/muic/s2mu004-muic.h, drivers/muic/s2mu004-muic.c) ---- */
#define MUIC_ADDR		0x3d
#define MUIC_ADC		0x61
#define MUIC_DEVICE_TYPE1	0x62
#define MUIC_DEVICE_TYPE2	0x63
#define MUIC_DEVICE_TYPE3	0x64
#define MUIC_CHG_TYPE		0x68
#define MUIC_REV_ID		0x82
#define MUIC_CTRL1		0xc7
#define MUIC_SW_CTRL		0xca
#define CTRL1_MANUAL_SW		(1 << 2)	/* 1 = auto switching, 0 = manual */
#define SW_DM_DP_MASK		0xfc
#define SW_USB			((1 << 5) | (1 << 2))	/* DM=USB, DP=USB */

static int i2c_fd = -1;

static int i2c_open_muic(void)
{
	DIR *d = opendir("/sys/bus/i2c/devices");
	struct dirent *e;
	char path[96], name[64];

	if (!d)
		return -1;
	while ((e = readdir(d))) {
		if (strncmp(e->d_name, "i2c-", 4))
			continue;
		snprintf(path, sizeof(path), "/sys/bus/i2c/devices/%s/name", e->d_name);
		read_file(path, name, sizeof(name));
		if (strstr(name, "muic")) {
			snprintf(path, sizeof(path), "/dev/%s", e->d_name);
			i2c_fd = open(path, O_RDWR);
			printf(" muic   : bus %s (%s)\n", e->d_name, name);
			break;
		}
	}
	closedir(d);
	return i2c_fd;
}

static int muic_rd(uint8_t reg)
{
	uint8_t val = 0;
	struct i2c_msg msgs[2] = {
		{ .addr = MUIC_ADDR, .flags = 0, .len = 1, .buf = &reg },
		{ .addr = MUIC_ADDR, .flags = I2C_M_RD, .len = 1, .buf = &val },
	};
	struct i2c_rdwr_ioctl_data x = { .msgs = msgs, .nmsgs = 2 };

	return ioctl(i2c_fd, I2C_RDWR, &x) == 2 ? val : -1;
}

static int muic_wr(uint8_t reg, uint8_t val)
{
	uint8_t buf[2] = { reg, val };
	struct i2c_msg msg = { .addr = MUIC_ADDR, .flags = 0, .len = 2, .buf = buf };
	struct i2c_rdwr_ioctl_data x = { .msgs = &msg, .nmsgs = 1 };

	return ioctl(i2c_fd, I2C_RDWR, &x) == 1 ? 0 : -1;
}

static const char *sw_path(int v)
{
	switch ((v >> 2) & 7) {	/* DP field; DM mirrors it for normal paths */
	case 0: return "OPEN";
	case 1: return "USB";
	case 2: return "UART";
	case 3: return "UART2";
	default: return "?";
	}
}

static int cmd_muic(void)
{
	int ctrl, sw, rev;

	if (i2c_open_muic() < 0) {
		printf(" muic   : no i2c-gpio-muic bus\n");
		return 1;
	}
	rev = muic_rd(MUIC_REV_ID);
	ctrl = muic_rd(MUIC_CTRL1);
	sw = muic_rd(MUIC_SW_CTRL);
	if (rev < 0 || ctrl < 0 || sw < 0) {
		printf(" muic   : I2C read FAILED (rev %d ctrl %d sw %d)\n", rev, ctrl, sw);
		return 1;
	}
	printf(" muic   : rev %02x adc %02x dev %02x %02x %02x chg %02x\n", rev,
	       muic_rd(MUIC_ADC), muic_rd(MUIC_DEVICE_TYPE1),
	       muic_rd(MUIC_DEVICE_TYPE2), muic_rd(MUIC_DEVICE_TYPE3),
	       muic_rd(MUIC_CHG_TYPE));
	printf(" muic   : CTRL1 %02x (%s switching)  SW_CTRL %02x (D+/D- = %s)\n",
	       ctrl, (ctrl & CTRL1_MANUAL_SW) ? "auto" : "manual", sw, sw_path(sw));

	if ((sw & SW_DM_DP_MASK) != SW_USB || (ctrl & CTRL1_MANUAL_SW)) {
		muic_wr(MUIC_SW_CTRL, (sw & 0x03) | SW_USB);
		muic_wr(MUIC_CTRL1, ctrl & ~CTRL1_MANUAL_SW);
		ctrl = muic_rd(MUIC_CTRL1);
		sw = muic_rd(MUIC_SW_CTRL);
		printf(" muic   : SET  CTRL1 %02x (%s)  SW_CTRL %02x (D+/D- = %s)\n",
		       ctrl, (ctrl & CTRL1_MANUAL_SW) ? "auto" : "manual", sw, sw_path(sw));
		return 10;	/* tell /init to re-attach the gadget */
	}
	return 0;
}

static int cmd_usbregs(void)
{
	volatile uint32_t *dwc = map(0x1360c000), *phy = map(0x135c0000);
	char st[64] = "";
	DIR *d = opendir("/sys/class/udc");
	struct dirent *e;

	while (d && (e = readdir(d))) {
		char path[96];

		if (e->d_name[0] == '.')
			continue;
		snprintf(path, sizeof(path), "/sys/class/udc/%s/state", e->d_name);
		read_file(path, st, sizeof(st));
	}
	if (d)
		closedir(d);
	printf(" udc    : state %s\n", st[0] ? st : "?");
	if (dwc)	/* DWC3 global/device registers at 0xc000 */
		printf(" dwc3   : GCTL %08x GSTS %08x U2PHY %08x\n"
		       "          DCFG %08x DCTL %08x DSTS %08x\n",
		       R(dwc, 0x110), R(dwc, 0x118), R(dwc, 0x200),
		       R(dwc, 0x700), R(dwc, 0x704), R(dwc, 0x70c));
	if (phy)	/* mainline phy-exynos5-usbdrd.c EXYNOS5/7870 offsets */
		printf(" phy    : LINKSYS %08x UTMI %08x CLKRST %08x\n"
		       "          HSPHYCTRL %08x PLLTUNE %08x\n",
		       R(phy, 0x04), R(phy, 0x08), R(phy, 0x10),
		       R(phy, 0x54), R(phy, 0x70));
	return 0;
}

static long long now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

static const char *key_name(int code)
{
	switch (code) {
	case KEY_POWER:		return "POWER";
	case KEY_VOLUMEUP:	return "VOLUME UP";
	case KEY_VOLUMEDOWN:	return "VOLUME DOWN";
	case KEY_HOMEPAGE:	return "HOME";
	default:		return "key";
	}
}

/*
 * K1: report the touch chain (PMIC on the gpm0 bus, touch controller on the
 * gpc2 bus), what bound to what, and the input devices. Always returns 0:
 * it only reports.
 */
static int cmd_touch(void)
{
	static const char *const devs[] = {
		"0-0066", "1-0066", "0-0020", "1-0020", "2-0066", "2-0020",
	};
	char path[300], line[160], link[200];
	struct dirent *e;
	DIR *d;

	for (unsigned int i = 0; i < sizeof(devs) / sizeof(devs[0]); i++) {
		ssize_t n;

		snprintf(path, sizeof(path), "/sys/bus/i2c/devices/%s", devs[i]);
		if (access(path, F_OK))
			continue;
		snprintf(path, sizeof(path), "/sys/bus/i2c/devices/%s/name", devs[i]);
		read_file(path, line, sizeof(line));
		snprintf(path, sizeof(path), "/sys/bus/i2c/devices/%s/driver", devs[i]);
		n = readlink(path, link, sizeof(link) - 1);
		link[n > 0 ? n : 0] = 0;
		printf(" touch  : i2c %s %s driver %s\n", devs[i],
		       line[0] ? line : "?", n > 0 ? strrchr(link, '/') + 1 : "NOT BOUND");
	}
	d = opendir("/sys/class/regulator");
	while (d && (e = readdir(d))) {
		if (strncmp(e->d_name, "regulator.", 10))
			continue;
		snprintf(path, sizeof(path), "/sys/class/regulator/%s/name", e->d_name);
		read_file(path, line, sizeof(line));
		if (strncmp(line, "vtouch", 6))
			continue;
		printf(" touch  : %s %s", e->d_name, line);
		snprintf(path, sizeof(path), "/sys/class/regulator/%s/microvolts", e->d_name);
		read_file(path, line, sizeof(line));
		printf("          %s uV\n", line[0] ? line : "?");
	}
	if (d)
		closedir(d);
	d = opendir("/sys/class/input");
	while (d && (e = readdir(d))) {
		if (strncmp(e->d_name, "input", 5))
			continue;
		snprintf(path, sizeof(path), "/sys/class/input/%s/name", e->d_name);
		read_file(path, line, sizeof(line));
		printf(" touch  : %s %s", e->d_name, line);
	}
	if (d)
		closedir(d);
	return 0;
}

static int cmd_keys(void)
{
	struct pollfd fds[8];
	int nfds = 0;
	long long power_down = -1;
	int tx = -1, ty = -1;

	for (int i = 0; i < 8; i++) {
		char path[32];
		int fd;

		snprintf(path, sizeof(path), "/dev/input/event%d", i);
		fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd >= 0) {
			fds[nfds].fd = fd;
			fds[nfds++].events = POLLIN;
		}
	}
	for (;;) {
		int r = poll(fds, nfds, 200);

		while (waitpid(-1, NULL, WNOHANG) > 0)
			;	/* PID 1 reaps orphans */
		if (power_down >= 0 && now_ms() - power_down >= 3000) {
			printf("\n POWER held 3 s -> rebooting into recovery (TWRP)...\n");
			fflush(stdout);
			sync();
			sleep(1);
			syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
				LINUX_REBOOT_CMD_RESTART2, "recovery");
		}
		if (r <= 0)
			continue;
		for (int i = 0; i < nfds; i++) {
			struct input_event ev;

			if (!(fds[i].revents & POLLIN))
				continue;
			while (read(fds[i].fd, &ev, sizeof(ev)) == sizeof(ev)) {
				if (ev.type == EV_ABS && ev.code == ABS_MT_POSITION_X)
					tx = ev.value;
				else if (ev.type == EV_ABS && ev.code == ABS_MT_POSITION_Y)
					ty = ev.value;
				else if (ev.type == EV_ABS && ev.code == ABS_MT_TRACKING_ID &&
					 ev.value < 0) {
					printf(" touch       up\n");
					fflush(stdout);
					tx = ty = -1;
				} else if (ev.type == EV_SYN && ev.code == SYN_REPORT &&
					   tx >= 0 && ty >= 0) {
					printf(" touch       %d,%d\n", tx, ty);
					fflush(stdout);
				}
				if (ev.type != EV_KEY || ev.value == 2)
					continue;
				printf(" %-11s %s\n", key_name(ev.code),
				       ev.value ? "pressed" : "released");
				fflush(stdout);
				if (ev.code == KEY_POWER)
					power_down = ev.value ? now_ms() : -1;
			}
		}
	}
}

/*
 * W0 checks for Waydroid. Read-only towards the hardware: it only mounts
 * binderfs (a pseudo filesystem) and asks binderfs for one device.
 */
static int cmd_reboot(const char *cmd)
{
	if (strcmp(cmd, "download") && strcmp(cmd, "recovery") &&
	    strcmp(cmd, "bootloader")) {
		fprintf(stderr, "reboot: CMD must be download, recovery or bootloader\n");
		return 1;
	}
	printf("rebooting: RESTART2 \"%s\"\n", cmd);
	fflush(stdout);
	sync();
	syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
		LINUX_REBOOT_CMD_RESTART2, cmd);
	perror("reboot");
	return 1;
}

static int cmd_android(void)
{
	struct drm_version v = { 0 };
	struct binderfs_device bd = { 0 };
	struct binder_version bv = { 0 };
	char drv[32] = "", line[160], path[300];
	int fd, ok = 1;
	DIR *d;
	struct dirent *e;

	/* KMS: simpledrm on the boot framebuffer */
	fd = open("/dev/dri/card0", O_RDWR);
	if (fd >= 0) {
		v.name = drv;
		v.name_len = sizeof(drv) - 1;
		if (ioctl(fd, DRM_IOCTL_VERSION, &v))
			drv[0] = 0;
		close(fd);
	}
	printf(" drm    : /dev/dri/card0 %s, driver %s\n",
	       fd >= 0 ? "OK" : "MISSING", drv[0] ? drv : "?");
	if (fd < 0)
		ok = 0;
	d = opendir("/sys/class/drm");
	while (d && (e = readdir(d))) {
		if (strncmp(e->d_name, "card0-", 6))
			continue;
		snprintf(path, sizeof(path), "/sys/class/drm/%s/status", e->d_name);
		read_file(path, line, sizeof(line));
		printf(" drm    : %s %s", e->d_name, line);
		snprintf(path, sizeof(path), "/sys/class/drm/%s/modes", e->d_name);
		read_file(path, line, sizeof(line));
		printf(", modes %s\n", line[0] ? line : "none");
	}
	if (d)
		closedir(d);

	/* binder through binderfs, as Waydroid uses it */
	mkdir("/dev/binderfs", 0755);
	if (mount("binder", "/dev/binderfs", "binder", 0, NULL) && errno != EBUSY) {
		printf(" binder : mount binderfs FAILED (%s)\n", strerror(errno));
		return 1;
	}
	fd = open("/dev/binderfs/binder-control", O_RDWR);
	if (fd < 0) {
		printf(" binder : binder-control MISSING\n");
		return 1;
	}
	strcpy(bd.name, "a5test");
	if (ioctl(fd, BINDER_CTL_ADD, &bd) && errno != EEXIST)
		printf(" binder : BINDER_CTL_ADD FAILED (%s)\n", strerror(errno));
	close(fd);
	fd = open("/dev/binderfs/a5test", O_RDWR | O_CLOEXEC);
	if (fd < 0 || ioctl(fd, BINDER_VERSION, &bv)) {
		printf(" binder : /dev/binderfs/a5test FAILED (%s)\n", strerror(errno));
		ok = 0;
	} else {
		printf(" binder : binderfs OK, device %u:%u, protocol %d (want %d)\n",
		       bd.major, bd.minor, bv.protocol_version,
		       BINDER_CURRENT_PROTOCOL_VERSION);
		if (bv.protocol_version != BINDER_CURRENT_PROTOCOL_VERSION)
			ok = 0;
	}
	if (fd >= 0)
		close(fd);

	/* the static devices pmaports' waydroid category asks for */
	for (int i = 0; i < 3; i++) {
		static const char *const dev[] = { "binder", "hwbinder", "vndbinder" };

		snprintf(path, sizeof(path), "/dev/binderfs/%s", dev[i]);
		fd = open(path, O_RDWR | O_CLOEXEC);
		if (fd < 0 || ioctl(fd, BINDER_VERSION, &bv)) {
			printf(" binder : %s FAILED\n", path);
			ok = 0;
		} else {
			printf(" binder : %s OK, protocol %d\n", path,
			       bv.protocol_version);
		}
		if (fd >= 0)
			close(fd);
	}

	/* PSI: Android's lmkd needs it */
	read_file("/proc/pressure/memory", line, sizeof(line));
	{
		char *nl = strchr(line, '\n');

		if (nl)
			*nl = 0;
	}
	printf(" psi    : %s\n", line[0] ? line : "MISSING");
	if (!line[0])
		ok = 0;
	printf(" w0     : %s\n", ok ? "PASS" : "FAIL");
	return !ok;
}

/* M5d/M5e boot choice: is a volume key held now or within the next ms? */
static int cmd_held(int ms)
{
	long long end = now_ms() + ms;
	int fds[8], nfds = 0;

	for (int i = 0; i < 8; i++) {
		char path[32];
		int fd;

		snprintf(path, sizeof(path), "/dev/input/event%d", i);
		fd = open(path, O_RDONLY);
		if (fd >= 0)
			fds[nfds++] = fd;
	}
	do {
		for (int i = 0; i < nfds; i++) {
			unsigned char keys[KEY_MAX / 8 + 1] = { 0 };

			if (ioctl(fds[i], EVIOCGKEY(sizeof(keys)), keys) < 0)
				continue;
			if (keys[KEY_VOLUMEDOWN / 8] & (1 << (KEY_VOLUMEDOWN % 8)))
				return 10;
			if (keys[KEY_VOLUMEUP / 8] & (1 << (KEY_VOLUMEUP % 8)))
				return 11;
		}
		usleep(50000);
	} while (now_ms() < end);
	return 1;
}

/*
 * M5e: let only one partition be written. bdev_read_only() of every bdev
 * includes get_disk_ro(), and that includes the whole-disk bdev's
 * BD_READ_ONLY (EVIDENCE 24): BLKROSET on /dev/mmcblk0 would make the kept
 * partition read-only too. So the whole-disk flag is cleared (a5-diag info
 * sets it), every other partition gets BLKROSET 1, the kept one 0, and then
 * the disk-wide force_ro goes to 0. The raw /dev/mmcblk0 node is therefore
 * writable (no flag can prevent that and keep one partition writable);
 * mmcblk0boot0/1 keep their own force_ro = 1.
 */
static int set_ro(const char *dev, int ro)
{
	int fd = open(dev, O_RDONLY), r, e;

	if (fd < 0) {
		printf(" lock   : open %s: %s\n", dev, strerror(errno));
		return -1;
	}
	r = ioctl(fd, BLKROSET, &ro);
	e = errno;
	close(fd);
	if (r)
		printf(" lock   : BLKROSET %s=%d: %s\n", dev, ro, strerror(e));
	else
		printf(" lock   : BLKROSET %s=%d ok\n", dev, ro);
	return r;
}

static int get_ro(const char *dev)
{
	int fd = open(dev, O_RDONLY), ro = -1;

	if (fd < 0 || ioctl(fd, BLKROGET, &ro))
		printf(" lock   : BLKROGET %s: %s\n", dev, strerror(errno));
	else
		printf(" lock   : BLKROGET %s = %d\n", dev, ro);
	if (fd >= 0)
		close(fd);
	return ro;
}

static int write_force_ro(const char *val)
{
	int fd = open("/sys/block/mmcblk0/force_ro", O_WRONLY), r = -1;

	if (fd >= 0) {
		r = write(fd, val, 1) == 1 ? 0 : -1;
		printf(" lock   : force_ro <- %s: %s\n", val, r ? strerror(errno) : "ok");
		close(fd);
	} else {
		printf(" lock   : open force_ro: %s\n", strerror(errno));
	}
	return r;
}

static int cmd_lock_except(const char *name)
{
	char path[64], val[8];
	int keep = find_part(name), bad = 0, parts = 0;

	if (keep < 0) {
		printf(" lock   : no %s partition, eMMC stays read-only\n", name);
		return 1;
	}
	read_file("/sys/block/mmcblk0/force_ro", val, sizeof(val));
	printf(" lock   : start: force_ro = %s, keep = mmcblk0p%d\n", val, keep);
	if (set_ro("/dev/mmcblk0", 0))
		bad++;
	for (int i = 1; i < 64; i++) {
		snprintf(path, sizeof(path), "/dev/mmcblk0p%d", i);
		if (access(path, F_OK))
			continue;
		/* the kept partition is set explicitly to 0: it may already
		 * carry BD_READ_ONLY from the partition scan */
		if (set_ro(path, i == keep ? 0 : 1))
			bad++;
	}
	if (bad) {
		printf(" lock   : BLKROSET failed on %d devices, eMMC stays read-only\n", bad);
		return 1;
	}
	if (write_force_ro("0")) {
		printf(" lock   : cannot clear force_ro\n");
		return 1;
	}
	read_file("/sys/block/mmcblk0/force_ro", val, sizeof(val));
	printf(" lock   : force_ro now reads %s\n", val);

	for (int i = 1; i < 64; i++) {
		snprintf(path, sizeof(path), "/sys/class/block/mmcblk0p%d/ro", i);
		if (access(path, F_OK))
			continue;
		parts++;
		read_file(path, val, sizeof(val));
		if (strcmp(val, i == keep ? "0" : "1")) {
			printf(" lock   : BAD mmcblk0p%d ro = '%s', want %s\n", i, val,
			       i == keep ? "0" : "1");
			bad++;
		} else {
			printf(" lock   : mmcblk0p%d ro = %s ok\n", i, val);
		}
	}
	/* the whole disk must now read 0 (its flag is what every partition
	 * inherits); report it, don't require it to be 1 */
	read_file("/sys/block/mmcblk0/ro", val, sizeof(val));
	printf(" lock   : /sys/block/mmcblk0/ro (disk flag) = %s\n", val);
	if (get_ro("/dev/mmcblk0") != 0) {
		printf(" lock   : BAD mmcblk0 BLKROGET != 0\n");
		bad++;
	}
	if (bad) {
		write_force_ro("1");
		printf(" lock   : check FAILED (%d), force_ro back to 1\n", bad);
		return 1;
	}
	printf(" lock   : mmcblk0p%d (%s) writable; the other %d partitions read-only (raw mmcblk0 node writable)\n",
	       keep, name, parts - 1);
	return 0;
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	if (argc < 2)
		goto usage;
	if (!strcmp(argv[1], "panel"))
		return cmd_panel();
	if (!strcmp(argv[1], "info"))
		return cmd_info();
	if (!strcmp(argv[1], "system"))
		return cmd_system();
	if (!strcmp(argv[1], "usb"))
		return cmd_usb();
	if (!strcmp(argv[1], "muic"))
		return cmd_muic();
	if (!strcmp(argv[1], "usbregs"))
		return cmd_usbregs();
	if (!strcmp(argv[1], "android"))
		return cmd_android();
	if (!strcmp(argv[1], "held") && argc > 2)
		return cmd_held(atoi(argv[2]));
	if (!strcmp(argv[1], "lock-except") && argc > 2)
		return cmd_lock_except(argv[2]);
	if (!strcmp(argv[1], "gpu"))
		return cmd_gpu(argc > 2 ? argv[2] : NULL);
	if (!strcmp(argv[1], "bt"))
		return cmd_bt();
	if (!strcmp(argv[1], "mmc1"))
		return cmd_mmc1();
	if (!strcmp(argv[1], "touch"))
		return cmd_touch();
	if (!strcmp(argv[1], "reboot") && argc > 2)
		return cmd_reboot(argv[2]);
	if (!strcmp(argv[1], "keys"))
		return cmd_keys();
usage:
	fprintf(stderr, "usage: a5-diag panel|info|system|usb|muic|usbregs|android|touch|mmc1|bt|gpu [status|on|clk|id]|held MS|lock-except NAME|reboot CMD|keys\n");
	return 2;
}
