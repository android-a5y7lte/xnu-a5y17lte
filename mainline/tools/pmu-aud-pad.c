/*
 * pmu-aud-pad [release|status]   Exynos 7880 PMU (physical 0x106b0000), mmap of /dev/mem.
 * "status" only reads. "release" does what the vendor audio driver (lpass_release_pad_reg) does when it powers the audio
 * subsystem: sets bit 28 of PAD_RETENTION_AUD_OPTION (0x3028) and bit 0 of GPIO_MODE_AUD_SYS_PWR_REG (0x1340), read-modify-write,
 * and prints both registers before and after. It can touch nothing else; a reboot restores the power-on state. EVIDENCE 83.
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define PMU_BASE	0x106b0000UL
#define PAD_RET_AUD	0x3028
#define GPIO_MODE_AUD	0x1340

static volatile uint32_t *r(void *m, unsigned int off, unsigned int page)
{
	return (volatile uint32_t *)((char *)m + (off - page));
}

int main(int argc, char **argv)
{
	int release = argc > 1 && !strcmp(argv[1], "release");
	int fd = open("/dev/mem", O_RDWR | O_SYNC);
	void *a, *b;
	volatile uint32_t *pad, *gm;

	if (fd < 0) {
		perror("/dev/mem");
		return 1;
	}
	a = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, PMU_BASE + 0x1000);	/* 0x1340 is in page 0x1000 */
	b = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, PMU_BASE + 0x3000);	/* 0x3028 is in page 0x3000 */
	if (a == MAP_FAILED || b == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	gm = r(a, GPIO_MODE_AUD, 0x1000);
	pad = r(b, PAD_RET_AUD, 0x3000);
	printf("before: GPIO_MODE_AUD_SYS_PWR_REG (0x1340) = 0x%08x, PAD_RETENTION_AUD_OPTION (0x3028) = 0x%08x\n", *gm, *pad);
	if (release) {
		*pad |= 0x10000000;
		*gm |= 0x1;
		usleep(10000);
		printf("after:  GPIO_MODE_AUD_SYS_PWR_REG (0x1340) = 0x%08x, PAD_RETENTION_AUD_OPTION (0x3028) = 0x%08x\n", *gm, *pad);
	}
	return 0;
}
