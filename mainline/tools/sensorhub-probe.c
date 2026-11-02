/*
 * sensorhub-probe: first contact with the STM32F410 sensor hub MCU of the Galaxy A5 (2017) through
 * spidev0.0 and the GPIO character device (no library). EVIDENCE 71/72.
 *
 *   sensorhub-probe [reset]     reset the MCU (boot0 low), wait, then WHOAMI, sensor scan and firmware revision
 *
 * Lines (all banks found by their chip label): reset gpf4-2, boot0 gpd2-0, ap_int gpd1-5 (outputs),
 * mcu_int1 gpa0-7 (data ready, active low), mcu_int2 gpa2-5 (MCU ready level, inputs).
 * Protocol as read from the vendor driver (EVIDENCE 71): 9-byte header cmd, length(u16), options(u16), data(u32),
 * little endian, ap_int low while the MCU is not busy (mcu_int2 low), write the header, ap_int high, wait for mcu_int2 high,
 * then the MCU pulls mcu_int1 low and the AP reads 4 bytes (options u16, length u16) and then the payload.
 * Output only goes to stdout. It never writes anything but the SPI header and the GPIOs above.
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <linux/spi/spidev.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

struct line { const char *bank; unsigned int off; int out; int fd; const char *name; };

static struct line L_RST = { "gpf4", 2, 1, -1, "reset" };
static struct line L_BOOT0 = { "gpd2", 0, 1, -1, "boot0" };
static struct line L_APINT = { "gpd1", 5, 1, -1, "ap_int" };
static struct line L_INT1 = { "gpa0", 7, 0, -1, "mcu_int1" };
static struct line L_INT2 = { "gpa2", 5, 0, -1, "mcu_int2" };

static void msleep(int ms)
{
	struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };

	nanosleep(&t, NULL);
}

static int open_line(struct line *l, int initial)
{
	char path[32];
	struct gpiochip_info ci;
	struct gpio_v2_line_request rq;
	int i, fd;

	for (i = 0; i < 16; i++) {
		snprintf(path, sizeof(path), "/dev/gpiochip%d", i);
		fd = open(path, O_RDWR);
		if (fd < 0)
			continue;
		memset(&ci, 0, sizeof(ci));
		if (!ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &ci) && !strcmp(ci.label, l->bank))
			break;
		close(fd);
		fd = -1;
	}
	if (fd < 0) {
		fprintf(stderr, "no gpiochip labelled %s\n", l->bank);
		return -1;
	}
	memset(&rq, 0, sizeof(rq));
	rq.offsets[0] = l->off;
	rq.num_lines = 1;
	snprintf(rq.consumer, sizeof(rq.consumer), "sensorhub-%s", l->name);
	if (l->out) {
		rq.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
		rq.config.num_attrs = 1;
		rq.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
		rq.config.attrs[0].attr.values = initial ? 1 : 0;
		rq.config.attrs[0].mask = 1;
	} else {
		rq.config.flags = GPIO_V2_LINE_FLAG_INPUT;
	}
	if (ioctl(fd, GPIO_V2_GET_LINE_IOCTL, &rq)) {
		fprintf(stderr, "request %s (%s-%u): %s\n", l->name, l->bank, l->off, strerror(errno));
		close(fd);
		return -1;
	}
	close(fd);
	l->fd = rq.fd;
	return 0;
}

static int get(struct line *l)
{
	struct gpio_v2_line_values v = { .bits = 0, .mask = 1 };

	if (ioctl(l->fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &v))
		return -1;
	return v.bits & 1;
}

static void set(struct line *l, int val)
{
	struct gpio_v2_line_values v = { .bits = val ? 1 : 0, .mask = 1 };

	ioctl(l->fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &v);
}

/* wait until a line has the value val; timeout in ms; returns 0 on success */
static int wait_level(struct line *l, int val, int timeout_ms)
{
	int n = timeout_ms * 2;

	while (n-- > 0) {
		if (get(l) == val)
			return 0;
		usleep(500);
	}
	return -1;
}

static void dump(const char *what, const uint8_t *b, int n)
{
	int i;

	printf("%s (%d):", what, n);
	for (i = 0; i < n && i < 64; i++)
		printf(" %02x", b[i]);
	printf(n > 64 ? " ...\n" : "\n");
}

static int spi_fd = -1;

static void status(const char *when)
{
	printf("[%s] mcu_int1=%d mcu_int2=%d\n", when, get(&L_INT1), get(&L_INT2));
}

/* send one request and print the answer; the answer length is len (for reads) */
static int request(uint8_t cmd, uint16_t len, uint16_t options, uint8_t *reply)
{
	uint8_t hdr[9] = { cmd, len & 0xff, len >> 8, options & 0xff, options >> 8, 0, 0, 0, 0 };
	uint8_t h4[4], buf[512];
	int tries;

	set(&L_APINT, 0);
	if (wait_level(&L_INT2, 0, 1500)) {
		printf("cmd 0x%02x: mcu_int2 never went low\n", cmd);
		set(&L_APINT, 1);
		return -1;
	}
	if (write(spi_fd, hdr, 9) != 9) {
		printf("cmd 0x%02x: spi write failed: %s\n", cmd, strerror(errno));
		set(&L_APINT, 1);
		return -1;
	}
	set(&L_APINT, 1);
	if (wait_level(&L_INT2, 1, 1500)) {
		printf("cmd 0x%02x: mcu_int2 never went high again\n", cmd);
		return -2;
	}
	for (tries = 0; tries < 20; tries++) {
		uint16_t o, l;

		if (wait_level(&L_INT1, 0, 1000)) {
			printf("cmd 0x%02x: no answer (mcu_int1 stays high)\n", cmd);
			return -3;
		}
		if (read(spi_fd, h4, 4) != 4) {
			printf("cmd 0x%02x: spi read of the 4 byte header failed\n", cmd);
			return -4;
		}
		o = h4[0] | (h4[1] << 8);
		l = h4[2] | (h4[3] << 8);
		printf("  msg: options=0x%04x length=%u\n", o, l);
		if (l > sizeof(buf))
			l = sizeof(buf);
		if ((o & 3) == 0 && l) {	/* AP2HUB_READ: the answer */
			if (read(spi_fd, buf, l) != l)
				return -5;
			dump("  answer", buf, l);
			if (reply)
				memcpy(reply, buf, l > 8 ? 8 : l);
			return l;
		}
		if ((o & 3) == 2 && l) {	/* HUB2AP_WRITE: a data frame from the MCU, keep waiting */
			if (read(spi_fd, buf, l) != l)
				return -5;
			dump("  hub frame", buf, l);
			continue;
		}
		printf("  unexpected message type %u\n", o & 3);
		return -6;
	}
	return -7;
}

int main(int argc, char **argv)
{
	int do_reset = argc > 1 && !strcmp(argv[1], "reset");
	uint8_t mode = SPI_MODE_3, bits = 8, reply[8];
	uint32_t speed = 2000000;
	int ret;

	if (open_line(&L_BOOT0, 0) || open_line(&L_APINT, 1) || open_line(&L_RST, 1) ||
	    open_line(&L_INT1, 0) || open_line(&L_INT2, 0))
		return 1;
	spi_fd = open("/dev/spidev2.0", O_RDWR);
	if (spi_fd < 0) {
		perror("/dev/spidev2.0");
		return 1;
	}
	if (ioctl(spi_fd, SPI_IOC_WR_MODE, &mode) || ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) ||
	    ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed)) {
		perror("spidev setup");
		return 1;
	}
	status("start");
	if (do_reset) {
		set(&L_RST, 0);
		msleep(2);
		set(&L_RST, 1);
		msleep(500);
		status("after reset + 500 ms");
	}

	printf("WHOAMI (0x0f), expect 0x55:\n");
	ret = request(0x0f, 1, 0, reply);
	if (ret > 0)
		printf("  -> chip id 0x%02x %s\n", reply[0], reply[0] == 0x55 ? "(ok)" : "(unexpected)");
	printf("sensor scan (0xf4):\n");
	ret = request(0xf4, 8, 0, reply);
	if (ret >= 8) {
		uint64_t m = 0;
		int i;

		for (i = 0; i < 8; i++)
			m |= (uint64_t)reply[i] << (8 * i);
		printf("  -> mask 0x%llx, sensor types present:", (unsigned long long)m);
		for (i = 0; i < 64; i++)
			if (m & (1ULL << i))
				printf(" %d", i);
		printf("\n");
	}
	printf("firmware revision (0xf0):\n");
	request(0xf0, 4, 0, reply);
	status("end");
	return 0;
}
