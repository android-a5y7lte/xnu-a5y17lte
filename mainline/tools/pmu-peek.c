/*
 * pmu-peek OFFSET...   read-only: print 32-bit registers of the Exynos 7880
 * PMU (physical 0x106b0000 + OFFSET) through an mmap of /dev/mem.
 * read() on /dev/mem fails with EFAULT for MMIO on arm64, only mmap works.
 * Never writes. EVIDENCE 27.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define PMU_BASE	0x106b0000UL
#define PMU_SIZE	0x10000UL

int main(int argc, char **argv)
{
	volatile uint32_t *pmu;
	int fd;

	if (argc < 2) {
		fprintf(stderr, "usage: pmu-peek OFFSET... (e.g. 0x404 0x808 0x80c)\n");
		return 2;
	}
	fd = open("/dev/mem", O_RDONLY | O_SYNC);
	if (fd < 0) {
		perror("open /dev/mem");
		return 1;
	}
	pmu = mmap(NULL, PMU_SIZE, PROT_READ, MAP_SHARED, fd, PMU_BASE);
	if (pmu == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	for (int i = 1; i < argc; i++) {
		unsigned long off = strtoul(argv[i], NULL, 0);

		if (off & 3 || off >= PMU_SIZE) {
			fprintf(stderr, "bad offset %s\n", argv[i]);
			return 2;
		}
		printf("PMU+0x%04lx = 0x%08x\n", off, pmu[off / 4]);
	}
	return 0;
}
