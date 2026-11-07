/*
 * i2c10-read BUS ADDR REG...   read-only: read single 8-bit registers of a device with a 10-bit I2C slave address
 * Environment I2C_7BIT=1 uses a plain 7-bit address instead (also the reserved ones).
 * (write the register number, repeated start, read one byte; the same transfer the vendor codec driver's regmap does).
 * Used for the COD3026x audio codec at 10-bit address 0x04 on the PMIC bus (EVIDENCE 80/81). Never writes a register.
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

int main(int argc, char **argv)
{
	char path[32];
	int fd, i;
	unsigned long funcs = 0;
	unsigned int addr;
	unsigned short ten;

	if (argc < 4) {
		fprintf(stderr, "usage: %s BUS ADDR REG...\n", argv[0]);
		return 2;
	}
	snprintf(path, sizeof(path), "/dev/i2c-%s", argv[1]);
	fd = open(path, O_RDWR);
	if (fd < 0) {
		perror(path);
		return 1;
	}
	addr = strtoul(argv[2], NULL, 0);
	ten = getenv("I2C_7BIT") ? 0 : I2C_M_TEN;
	if (!ten && ioctl(fd, I2C_SLAVE_FORCE, addr) < 0)
		perror("I2C_SLAVE_FORCE");
	if (!ioctl(fd, I2C_FUNCS, &funcs))
		printf("adapter 10-bit support: %s\n", (funcs & I2C_FUNC_10BIT_ADDR) ? "yes" : "NO");
	for (i = 3; i < argc; i++) {
		uint8_t reg = strtoul(argv[i], NULL, 0), val = 0;
		struct i2c_msg m[2] = {
			{ .addr = addr, .flags = ten, .len = 1, .buf = &reg },
			{ .addr = addr, .flags = ten | I2C_M_RD, .len = 1, .buf = &val },
		};
		struct i2c_rdwr_ioctl_data d = { .msgs = m, .nmsgs = 2 };

		if (ioctl(fd, I2C_RDWR, &d) < 0)
			printf("reg 0x%02x: %s\n", reg, strerror(errno));
		else
			printf("reg 0x%02x = 0x%02x\n", reg, val);
	}
	return 0;
}
