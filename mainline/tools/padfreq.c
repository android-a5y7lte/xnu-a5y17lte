/*
 * padfreq: sample the data register of GPIO bank gpz1 (DISPAUD pin controller, physical 0x148c0024) as fast as the bus allows and
 * print, for each of the six pads (the audio I2S pads of the Exynos 7880), the number of transitions and an estimate of the toggle
 * frequency. Read only; used to see whether the audio mixer drives clocks and data towards the codec (EVIDENCE 94).
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define N (1 << 20)

int main(void)
{
	int fd = open("/dev/mem", O_RDONLY | O_SYNC), p, i;
	volatile uint32_t *dat;
	uint8_t *buf = malloc(N);
	struct timespec a, b;
	double dt;
	unsigned long trans[6] = { 0 }, high[6] = { 0 };

	if (fd < 0 || !buf) {
		perror("setup");
		return 1;
	}
	dat = (volatile uint32_t *)((char *)mmap(NULL, 0x1000, PROT_READ, MAP_SHARED, fd, 0x148c0000) + 0x24);
	if ((void *)dat == (void *)(0x24 + (char *)MAP_FAILED)) {
		perror("mmap");
		return 1;
	}
	clock_gettime(CLOCK_MONOTONIC, &a);
	for (i = 0; i < N; i++)
		buf[i] = *dat & 0x3f;
	clock_gettime(CLOCK_MONOTONIC, &b);
	dt = (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9;
	for (i = 1; i < N; i++)
		for (p = 0; p < 6; p++) {
			if (((buf[i] ^ buf[i - 1]) >> p) & 1)
				trans[p]++;
			high[p] += (buf[i] >> p) & 1;
		}
	printf("%d samples in %.4f s = %.2f MHz sampling\n", N, dt, N / dt / 1e6);
	for (p = 0; p < 6; p++)
		printf("gpz1-%d: %7lu transitions, high %5.1f%%, toggle rate ~ %.1f kHz\n", p, trans[p], 100.0 * high[p] / N,
		       trans[p] / dt / 2000.0);
	return 0;
}
