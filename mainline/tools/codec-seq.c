/*
 * codec-seq BUS ADDR STEP...   run a register sequence on an 8-bit-register I2C device with a plain 7-bit address (also the
 * reserved ones, forced), for the COD3026x audio codec (address 0x04 on the PMIC bus, EVIDENCE 81/89). Steps:
 *   r:REG              read and print
 *   w:REG:VAL          write
 *   u:REG:MASK:VAL     read-modify-write: (old & ~MASK) | (VAL & MASK)
 *   g:REG:MASK         remember (old & MASK) as the "gain" (for a later "p")
 *   p:REG:MASK         restore: update REG with the remembered gain under MASK
 *   ms:N               sleep N milliseconds
 * Numbers are C-style (0x.. allowed). A write is only done on explicit w/u/p steps.
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int fd;
static unsigned int addr;

static int rd(uint8_t reg, uint8_t *val)
{
	struct i2c_msg m[2] = {
		{ .addr = addr, .flags = 0, .len = 1, .buf = &reg },
		{ .addr = addr, .flags = I2C_M_RD, .len = 1, .buf = val },
	};
	struct i2c_rdwr_ioctl_data d = { .msgs = m, .nmsgs = 2 };

	return ioctl(fd, I2C_RDWR, &d);
}

static int wr(uint8_t reg, uint8_t val)
{
	uint8_t b[2] = { reg, val };
	struct i2c_msg m = { .addr = addr, .flags = 0, .len = 2, .buf = b };
	struct i2c_rdwr_ioctl_data d = { .msgs = &m, .nmsgs = 1 };

	return ioctl(fd, I2C_RDWR, &d);
}

int main(int argc, char **argv)
{
	char path[32];
	uint8_t gain = 0;
	int i;

	if (argc < 4) {
		fprintf(stderr, "usage: %s BUS ADDR STEP...\n", argv[0]);
		return 2;
	}
	snprintf(path, sizeof(path), "/dev/i2c-%s", argv[1]);
	fd = open(path, O_RDWR);
	if (fd < 0) {
		perror(path);
		return 1;
	}
	addr = strtoul(argv[2], NULL, 0);
	if (ioctl(fd, I2C_SLAVE_FORCE, addr) < 0)
		perror("I2C_SLAVE_FORCE");
	for (i = 3; i < argc; i++) {
		char *s = argv[i], *a, *b;
		unsigned int reg, x, y;
		uint8_t v;

		if (!strncmp(s, "ms:", 3)) {
			usleep(strtoul(s + 3, NULL, 0) * 1000);
			continue;
		}
		if (s[1] != ':') {
			fprintf(stderr, "bad step %s\n", s);
			return 2;
		}
		reg = strtoul(s + 2, &a, 0);
		x = *a == ':' ? strtoul(a + 1, &b, 0) : 0;
		y = (*a == ':' && *b == ':') ? strtoul(b + 1, NULL, 0) : 0;
		switch (s[0]) {
		case 'r':
			if (rd(reg, &v) < 0) { printf("r 0x%02x: %s\n", reg, strerror(errno)); return 1; }
			printf("r 0x%02x = 0x%02x\n", reg, v);
			break;
		case 'w':
			if (wr(reg, x) < 0) { printf("w 0x%02x: %s\n", reg, strerror(errno)); return 1; }
			printf("w 0x%02x <- 0x%02x\n", reg, x);
			break;
		case 'u':
			if (rd(reg, &v) < 0) { printf("u 0x%02x: read %s\n", reg, strerror(errno)); return 1; }
			if (wr(reg, (v & ~x) | (y & x)) < 0) { printf("u 0x%02x: write %s\n", reg, strerror(errno)); return 1; }
			printf("u 0x%02x: 0x%02x -> 0x%02x\n", reg, v, (v & ~x) | (y & x));
			break;
		case 'g':
			if (rd(reg, &v) < 0) { printf("g 0x%02x: %s\n", reg, strerror(errno)); return 1; }
			gain = v & x;
			printf("g 0x%02x: gain 0x%02x\n", reg, gain);
			break;
		case 'p':
			if (rd(reg, &v) < 0) { printf("p 0x%02x: read %s\n", reg, strerror(errno)); return 1; }
			if (wr(reg, (v & ~x) | gain) < 0) { printf("p 0x%02x: write %s\n", reg, strerror(errno)); return 1; }
			printf("p 0x%02x: 0x%02x -> 0x%02x\n", reg, v, (v & ~x) | gain);
			break;
		default:
			fprintf(stderr, "bad step %s\n", s);
			return 2;
		}
	}
	return 0;
}
