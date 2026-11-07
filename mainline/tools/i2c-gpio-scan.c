/*
 * i2c-gpio-scan write BANK SDA SCL ADDR REG VALUE16   write one 16-bit register (the only write operation).
 * i2c-gpio-scan BANK SDA SCL ADDR REG [N]   read N bytes (default 2) from register REG of device ADDR (reads only).
 * i2c-gpio-scan BANK LINE_A LINE_B   scan an I2C bus bit-banged on two GPIO lines of one bank (GPIO character device v2, open drain with
 * read-back, about 10 kHz), once with A = SDA / B = SCL and once swapped, and print the 7-bit addresses (0x08-0x77) that acknowledge a write address
 * byte. For the speaker amplifier bus gpc9-0 / gpc9-1 of the Galaxy A5 (2017) (EVIDENCE 95/96). It only sends address bytes, never data.
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int sda_fd = -1, scl_fd = -1;
static int dly = 25;

static int req(const char *bank, unsigned int off, const char *name)
{
	char path[32];
	struct gpiochip_info ci;
	struct gpio_v2_line_request rq;
	int i, fd;

	for (i = 0; i < 64; i++) {
		snprintf(path, sizeof(path), "/dev/gpiochip%d", i);
		fd = open(path, O_RDWR);
		if (fd < 0)
			continue;
		memset(&ci, 0, sizeof(ci));
		if (!ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &ci) && !strcmp(ci.label, bank))
			break;
		close(fd);
		fd = -1;
	}
	if (fd < 0) {
		fprintf(stderr, "no gpiochip %s\n", bank);
		exit(1);
	}
	memset(&rq, 0, sizeof(rq));
	rq.offsets[0] = off;
	rq.num_lines = 1;
	snprintf(rq.consumer, sizeof(rq.consumer), "i2cscan-%s", name);
	rq.config.flags = GPIO_V2_LINE_FLAG_OUTPUT | GPIO_V2_LINE_FLAG_OPEN_DRAIN;
	rq.config.num_attrs = 1;
	rq.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
	rq.config.attrs[0].attr.values = 1;
	rq.config.attrs[0].mask = 1;
	if (ioctl(fd, GPIO_V2_GET_LINE_IOCTL, &rq)) {
		fprintf(stderr, "request %s-%u: %s\n", bank, off, strerror(errno));
		exit(1);
	}
	close(fd);
	return rq.fd;
}

static void setl(int fd, int v)
{
	struct gpio_v2_line_values lv = { .bits = v ? 1 : 0, .mask = 1 };

	ioctl(fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &lv);
}

/* read a released line: switch it to input for the read, then back to open-drain high */
static int getl(int fd)
{
	struct gpio_v2_line_values lv = { .bits = 0, .mask = 1 };
	struct gpio_v2_line_config c;

	memset(&c, 0, sizeof(c));
	c.flags = GPIO_V2_LINE_FLAG_INPUT;
	ioctl(fd, GPIO_V2_LINE_SET_CONFIG_IOCTL, &c);
	ioctl(fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &lv);
	memset(&c, 0, sizeof(c));
	c.flags = GPIO_V2_LINE_FLAG_OUTPUT | GPIO_V2_LINE_FLAG_OPEN_DRAIN;
	c.num_attrs = 1;
	c.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
	c.attrs[0].attr.values = 1;
	c.attrs[0].mask = 1;
	ioctl(fd, GPIO_V2_LINE_SET_CONFIG_IOCTL, &c);
	return lv.bits & 1;
}

#define SDA(v) setl(sda_fd, v)
#define SCL(v) setl(scl_fd, v)
#define D() usleep(dly)

static void start(void) { SDA(1); SCL(1); D(); SDA(0); D(); SCL(0); D(); }
static void stop(void) { SDA(0); D(); SCL(1); D(); SDA(1); D(); }

static int wbyte(unsigned int b)
{
	int i, ack;

	for (i = 7; i >= 0; i--) {
		SDA((b >> i) & 1); D();
		SCL(1); D();
		SCL(0); D();
	}
	SDA(1); D();
	SCL(1); D();
	ack = !getl(sda_fd);
	SCL(0); D();
	return ack;
}

static int rbyte(int ack)
{
	int i, v = 0;

	SDA(1); D();
	for (i = 0; i < 8; i++) {
		SCL(1); D();
		v = (v << 1) | getl(sda_fd);
		SCL(0); D();
	}
	SDA(ack ? 0 : 1); D();
	SCL(1); D();
	SCL(0); D();
	SDA(1); D();
	return v;
}

/* read N bytes starting at register REG of device ADDR: address + register, repeated start, address + read */
static void readregs(const char *bank, int a, int b, unsigned int addr, unsigned int reg, int n)
{
	int i;

	sda_fd = req(bank, a, "sda");
	scl_fd = req(bank, b, "scl");
	SDA(1); SCL(1); D();
	start();
	if (!wbyte(addr << 1) || !wbyte(reg)) {
		printf("no ACK for 0x%02x reg 0x%02x\n", addr, reg);
		stop();
		return;
	}
	SDA(1); D(); SCL(1); D(); SDA(0); D(); SCL(0); D();	/* repeated start */
	if (!wbyte((addr << 1) | 1)) {
		printf("no ACK for the read address 0x%02x\n", addr);
		stop();
		return;
	}
	printf("0x%02x reg 0x%02x:", addr, reg);
	for (i = 0; i < n; i++)
		printf(" %02x", rbyte(i < n - 1));
	printf("\n");
	stop();
	close(sda_fd);
	close(scl_fd);
}

/* write one 16-bit register (big endian) of an amplifier-style device: address, register, high byte, low byte */
static void writereg16(const char *bank, int a, int b, unsigned int addr, unsigned int reg, unsigned int val)
{
	sda_fd = req(bank, a, "sda");
	scl_fd = req(bank, b, "scl");
	SDA(1); SCL(1); D();
	start();
	if (!wbyte(addr << 1) || !wbyte(reg) || !wbyte(val >> 8) || !wbyte(val & 0xff))
		printf("write 0x%02x reg 0x%02x: missing ACK\n", addr, reg);
	else
		printf("wrote 0x%02x reg 0x%02x <- 0x%04x\n", addr, reg, val);
	stop();
	close(sda_fd);
	close(scl_fd);
}

static void scan(const char *bank, int a, int b)
{
	unsigned int addr;
	int n = 0;

	sda_fd = req(bank, a, "sda");
	scl_fd = req(bank, b, "scl");
	SDA(1); SCL(1); D();
	printf("SDA = %s-%d, SCL = %s-%d: idle levels SDA=%d SCL=%d\n", bank, a, bank, b, getl(sda_fd), getl(scl_fd));
	for (addr = 0x08; addr <= 0x77; addr++) {
		start();
		if (wbyte(addr << 1)) {
			printf("  ACK at 0x%02x\n", addr);
			n++;
		}
		stop();
	}
	printf("  %d device(s)\n", n);
	close(sda_fd);
	close(scl_fd);
}

int main(int argc, char **argv)
{
	if (argc < 4 || (argc >= 7 && !strcmp(argv[1], "write") && argc < 8)) {
		fprintf(stderr, "usage: %s BANK LINE_A LINE_B\n", argv[0]);
		return 2;
	}
	if (argc >= 7 && !strcmp(argv[1], "write")) {	/* write BANK SDA SCL ADDR REG VALUE16 */
		writereg16(argv[2], atoi(argv[3]), atoi(argv[4]), strtoul(argv[5], NULL, 0), strtoul(argv[6], NULL, 0),
			   strtoul(argv[7], NULL, 0));
		return 0;
	}
	if (argc >= 6) {	/* BANK SDA SCL ADDR REG [N]: read registers with A = SDA, B = SCL as given */
		readregs(argv[1], atoi(argv[2]), atoi(argv[3]), strtoul(argv[4], NULL, 0), strtoul(argv[5], NULL, 0),
			 argc > 6 ? atoi(argv[6]) : 2);
		return 0;
	}
	scan(argv[1], atoi(argv[2]), atoi(argv[3]));
	scan(argv[1], atoi(argv[3]), atoi(argv[2]));
	return 0;
}
