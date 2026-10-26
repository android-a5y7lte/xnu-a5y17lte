/*
 * pmu-poke OFFSET VALUE   write one 32-bit PMU INFORM scratch register of the
 * Exynos 7880 (physical 0x106b0000 + OFFSET) through an mmap of /dev/mem, then
 * read it back. Only the INFORM block 0x800-0x81c is accepted, so the reset
 * register (0x400) and the power-domain registers cannot be hit by mistake.
 * Companion of pmu-peek (read-only). EVIDENCE 27.
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
#define INFORM_FIRST	0x800UL
#define INFORM_LAST	0x81cUL

int main(int argc, char **argv)
{
	volatile uint32_t *pmu;
	unsigned long off, val;
	char *end;
	int fd;

	if (argc != 3) {
		fprintf(stderr, "usage: pmu-poke OFFSET VALUE (OFFSET 0x800-0x81c, e.g. 0x80c 0x12345670)\n");
		return 2;
	}
	off = strtoul(argv[1], &end, 0);
	if (*end || off & 3 || off < INFORM_FIRST || off > INFORM_LAST) {
		fprintf(stderr, "bad offset %s (only INFORM 0x800-0x81c)\n", argv[1]);
		return 2;
	}
	val = strtoul(argv[2], &end, 0);
	if (*end || val > 0xffffffffUL) {
		fprintf(stderr, "bad value %s\n", argv[2]);
		return 2;
	}
	fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (fd < 0) {
		perror("open /dev/mem");
		return 1;
	}
	pmu = mmap(NULL, PMU_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
		   PMU_BASE);
	if (pmu == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	printf("PMU+0x%04lx was 0x%08x\n", off, pmu[off / 4]);
	pmu[off / 4] = (uint32_t)val;
	printf("PMU+0x%04lx now 0x%08x\n", off, pmu[off / 4]);
	return 0;
}
