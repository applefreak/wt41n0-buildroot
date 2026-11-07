// SPDX-License-Identifier: GPL-2.0
/*
 * wt41n0-scan: read barcodes from the WT41N0 ring scanner (see docs/hardware.md,
 * "Ring scanner").
 *
 * The scanner speaks Zebra SSI on UART2 (/dev/ttyS1) at 38400 8N1. It needs the
 * FPGA outputs "accessory-power" and "accessory-serial" on, and RTS deasserted:
 * it does not answer while RTS is asserted. Its trigger is wired to the
 * terminal (OMAP GPIO 2, active low), so the host starts and stops each decode
 * session: START_DECODE on press, STOP_DECODE on release.
 *
 * Usage: wt41n0-scan [-1] [-s] [-k] [-v]
 *   (default)  print one line per barcode, scanning while the trigger is held
 *   -1         exit after the first barcode
 *   -s         start a scan now instead of waiting for the trigger (implies -1;
 *              gives up after 10 s)
 *   -k         keep the accessory port powered on exit
 *   -v         show SSI packets and the barcode type on stderr
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define TTY		"/dev/ttyS1"
#define TRIGGER_CHIP	"gpio-0-31"	/* OMAP GPIO bank 1 */
#define TRIGGER_LINE	2

/* SSI opcodes */
#define SSI_CMD_ACK	0xD0
#define SSI_CMD_NAK	0xD1
#define SSI_DECODE_DATA	0xF3
#define SSI_PARAM_SEND	0xC6
#define SSI_REQ_REV	0xA3
#define SSI_REPLY_REV	0xA4
#define SSI_START	0xE4
#define SSI_STOP	0xE5
#define SSI_HOST	0x04	/* message source: host */
#define SSI_STATUS_CONT	0x02	/* more packets follow */
#define PARAM_PACKETED	0xEE	/* "decode data packet format": 1 = packeted */
#define PARAM_CODE_ID	0x2D	/* "transmit code ID character": 0 = none */

static int verbose;
static volatile sig_atomic_t stop;

static void die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fputs("wt41n0-scan: ", stderr);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(1);
}

static void on_signal(int sig)
{
	(void)sig;
	stop = 1;
}

static long now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000;
}

/* ---- FPGA outputs, through sysfs like wt41n0-fpga --------------------------- */

static int fpga_base = -1;

static void fpga_output(const char *name, int line, int value)
{
	char path[320], buf[16];
	FILE *f;

	if (fpga_base < 0) {
		DIR *d = opendir("/sys/class/gpio");
		struct dirent *e;

		if (!d)
			die("no /sys/class/gpio");
		while ((e = readdir(d)) && fpga_base < 0) {
			if (strncmp(e->d_name, "gpiochip", 8))
				continue;
			snprintf(path, sizeof(path), "/sys/class/gpio/%s/label", e->d_name);
			f = fopen(path, "r");
			if (f && fgets(buf, sizeof(buf), f) && !strcmp(buf, "wt41n0-fpga\n"))
				fpga_base = atoi(e->d_name + 8);
			if (f)
				fclose(f);
		}
		closedir(d);
		if (fpga_base < 0)
			die("no wt41n0-fpga GPIO chip");
	}

	/* sysfs names exported lines by their line name */
	snprintf(path, sizeof(path), "/sys/class/gpio/%s/value", name);
	if (access(path, F_OK)) {
		f = fopen("/sys/class/gpio/export", "w");
		if (!f || fprintf(f, "%d", fpga_base + line) < 0 || fclose(f))
			die("cannot export %s", name);
	}
	f = fopen(path, "w");
	if (!f || fprintf(f, "%d", value) < 0 || fclose(f))
		die("cannot set %s: %s", name, strerror(errno));
}

static void port_power(int on)
{
	fpga_output("accessory-power", 0, on);
	fpga_output("accessory-serial", 5, on);
}

/* ---- trigger: GPIO character device, both edges ----------------------------- */

static int trigger_open(void)
{
	struct gpioevent_request req;
	struct gpiochip_info info;
	char path[32];
	int i, fd;

	for (i = 0; i < 16; i++) {
		snprintf(path, sizeof(path), "/dev/gpiochip%d", i);
		fd = open(path, O_RDWR | O_CLOEXEC);
		if (fd < 0)
			continue;
		if (!ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &info) &&
		    !strcmp(info.label, TRIGGER_CHIP))
			break;
		close(fd);
		fd = -1;
	}
	if (fd < 0)
		die("no GPIO chip \"%s\"", TRIGGER_CHIP);

	/* the v1 interface: the toolchain's kernel headers predate v2 */
	memset(&req, 0, sizeof(req));
	req.lineoffset = TRIGGER_LINE;
	req.handleflags = GPIOHANDLE_REQUEST_INPUT;
	req.eventflags = GPIOEVENT_REQUEST_BOTH_EDGES;
	strcpy(req.consumer_label, "ring-scanner-trigger");
	if (ioctl(fd, GPIO_GET_LINEEVENT_IOCTL, &req))
		die("cannot request the trigger line: %s", strerror(errno));
	close(fd);
	return req.fd;
}

/*
 * The trigger contact bounces: after an edge, drain the queued events, let the
 * line settle and read its level. Returns 1 while pressed (the line is low).
 */
static int trigger_state(int fd)
{
	struct gpiohandle_data v;
	struct gpioevent_data ev;
	struct pollfd pfd = { .fd = fd, .events = POLLIN };

	do {
		if (read(fd, &ev, sizeof(ev)) != sizeof(ev))
			die("trigger read: %s", strerror(errno));
	} while (poll(&pfd, 1, 20) > 0);
	if (ioctl(fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &v))
		die("trigger level: %s", strerror(errno));
	return !v.values[0];
}

/* ---- SSI over the serial port ----------------------------------------------- */

static int tty_open(void)
{
	struct termios t;
	int fd, rts = TIOCM_RTS;

	fd = open(TTY, O_RDWR | O_NOCTTY | O_CLOEXEC);
	if (fd < 0)
		die("cannot open %s: %s", TTY, strerror(errno));
	if (tcgetattr(fd, &t))
		die("tcgetattr: %s", strerror(errno));
	cfmakeraw(&t);
	t.c_cflag &= ~(CSTOPB | PARENB | CRTSCTS);
	t.c_cflag |= CLOCAL | CREAD | CS8;
	t.c_cc[VMIN] = 0;
	t.c_cc[VTIME] = 0;
	cfsetispeed(&t, B38400);
	cfsetospeed(&t, B38400);
	if (tcsetattr(fd, TCSANOW, &t))
		die("tcsetattr: %s", strerror(errno));
	/* the scanner stays silent while RTS is asserted */
	if (ioctl(fd, TIOCMBIC, &rts))
		die("cannot clear RTS: %s", strerror(errno));
	tcflush(fd, TCIOFLUSH);
	return fd;
}

static void dump(const char *dir, const unsigned char *p, int n)
{
	int i;

	if (!verbose)
		return;
	fprintf(stderr, "%s", dir);
	for (i = 0; i < n; i++)
		fprintf(stderr, " %02x", p[i]);
	fputc('\n', stderr);
}

static void ssi_send(int fd, unsigned char opcode, const unsigned char *data, int n)
{
	unsigned char p[64] = { 0 };
	unsigned int sum = 0;
	int i, len = 4 + n;

	p[0] = len;
	p[1] = opcode;
	p[2] = SSI_HOST;
	p[3] = 0;	/* status: first transmission, temporary parameters */
	memcpy(p + 4, data, n);
	for (i = 0; i < len; i++)
		sum += p[i];
	sum = (0x10000 - sum) & 0xffff;
	p[len] = sum >> 8;
	p[len + 1] = sum & 0xff;

	/* wake the scanner if it sleeps; it ignores the byte when awake */
	if (write(fd, "", 1) != 1)
		die("write: %s", strerror(errno));
	usleep(20000);
	dump(">", p, len + 2);
	if (write(fd, p, len + 2) != len + 2)
		die("write: %s", strerror(errno));
}

static void ssi_ack(int fd)
{
	ssi_send(fd, SSI_CMD_ACK, NULL, 0);
}

/*
 * Read one packet into p (at least 258 bytes) within timeout_ms. Returns its
 * length without the checksum, 0 on timeout, -1 for a bad packet.
 */
static int ssi_recv(int fd, unsigned char *p, int timeout_ms)
{
	long end = now_ms() + timeout_ms;
	unsigned int sum = 0;
	int got = 0, want = 1, i;

	while (got < want) {
		struct pollfd pfd = { .fd = fd, .events = POLLIN };
		long left = end - now_ms();
		int n;

		if (left <= 0 || poll(&pfd, 1, left) <= 0)
			return got ? -1 : 0;
		n = read(fd, p + got, want - got);
		if (n <= 0)
			continue;
		if (!got && p[0] < 4)	/* not a length byte (line noise) */
			continue;
		got += n;
		if (got >= 1)
			want = p[0] + 2;
		end = now_ms() + 200;	/* bytes of one packet arrive together */
	}
	dump("<", p, got);
	for (i = 0; i < p[0]; i++)
		sum += p[i];
	if (((sum + (p[p[0]] << 8 | p[p[0] + 1])) & 0xffff) != 0)
		return -1;
	return p[0];
}

/* send a command and wait for its ACK; returns 0 on success */
static int ssi_command(int fd, unsigned char opcode, const unsigned char *data, int n)
{
	unsigned char p[260];
	int len;

	ssi_send(fd, opcode, data, n);
	for (;;) {
		len = ssi_recv(fd, p, 1000);
		if (len <= 0)
			return -1;
		if (p[1] == SSI_CMD_ACK)
			return 0;
		if (p[1] == SSI_CMD_NAK) {
			fprintf(stderr, "wt41n0-scan: command 0x%02x refused (NAK 0x%02x)\n",
				opcode, len > 4 ? p[4] : 0);
			return -1;
		}
		ssi_ack(fd);	/* an unrelated message; acknowledge it */
	}
}

/* wait until the scanner answers a revision request (it boots after power-on) */
static void ssi_wait_ready(int fd)
{
	unsigned char p[260];
	long end = now_ms() + 5000;
	int n;

	while (now_ms() < end) {
		ssi_send(fd, SSI_REQ_REV, NULL, 0);
		while (ssi_recv(fd, p, 500) > 0) {
			if (p[1] == SSI_REPLY_REV)
				goto ready;
			if (p[1] != SSI_CMD_ACK && p[1] != SSI_CMD_NAK)
				ssi_ack(fd);	/* e.g. the power-up event */
		}
	}
	die("no answer from the ring scanner (is it plugged in?)");

ready:
	/* the revision text is followed by binary fields */
	for (n = 0; n < p[0] - 4 && p[4 + n] >= 0x20 && p[4 + n] < 0x7f; n++)
		;
	while (n && p[3 + n] == ' ')
		n--;
	if (verbose)
		fprintf(stderr, "scanner: %.*s\n", n, p + 4);
}

int main(int argc, char **argv)
{
	unsigned char p[260], code[1024];
	int opt, once = 0, soft = 0, keep = 0, tty, trig = -1, codelen = 0, ret = 0, pressed = 0;
	const unsigned char params[] = { 0xFF /* no beep */, PARAM_PACKETED, 0x01, PARAM_CODE_ID, 0x00 };
	long deadline = 0;

	while ((opt = getopt(argc, argv, "1skv")) != -1) {
		switch (opt) {
		case '1': once = 1; break;
		case 's': soft = once = 1; break;
		case 'k': keep = 1; break;
		case 'v': verbose = 1; break;
		default:
			fprintf(stderr, "usage: %s [-1] [-s] [-k] [-v]\n", argv[0]);
			return 2;
		}
	}
	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);

	port_power(1);
	tty = tty_open();
	ssi_wait_ready(tty);
	if (ssi_command(tty, SSI_PARAM_SEND, params, sizeof(params)))
		die("cannot set the scanner parameters");

	if (soft) {
		if (ssi_command(tty, SSI_START, NULL, 0))
			die("the scanner refused START_DECODE");
		deadline = now_ms() + 10000;
	} else {
		trig = trigger_open();
	}

	while (!stop) {
		struct pollfd pfd[2] = {
			{ .fd = tty, .events = POLLIN },
			{ .fd = trig, .events = POLLIN },
		};
		int timeout = deadline ? (int)(deadline - now_ms()) : -1;

		if (deadline && timeout <= 0) {
			fprintf(stderr, "wt41n0-scan: no barcode read\n");
			ret = 1;
			break;
		}
		if (poll(pfd, trig >= 0 ? 2 : 1, timeout) < 0) {
			if (errno == EINTR)
				continue;
			die("poll: %s", strerror(errno));
		}
		if (trig >= 0 && (pfd[1].revents & POLLIN)) {
			int state = trigger_state(trig);

			if (state != pressed)
				ssi_send(tty, state ? SSI_START : SSI_STOP, NULL, 0);
			pressed = state;
		}
		if (!(pfd[0].revents & POLLIN))
			continue;

		int len = ssi_recv(tty, p, 500);

		if (len <= 0 || p[1] == SSI_CMD_ACK)
			continue;
		if (p[1] == SSI_CMD_NAK) {
			fprintf(stderr, "wt41n0-scan: NAK 0x%02x\n", len > 4 ? p[4] : 0);
			continue;
		}
		ssi_ack(tty);
		if (p[1] != SSI_DECODE_DATA || len < 5)
			continue;
		/* data: barcode type, then the decoded bytes; long codes span packets */
		if (!codelen && verbose)
			fprintf(stderr, "barcode type 0x%02x\n", p[4]);
		if (codelen + len - 5 <= (int)sizeof(code)) {
			memcpy(code + codelen, p + 5, len - 5);
			codelen += len - 5;
		}
		if (p[3] & SSI_STATUS_CONT)
			continue;
		fwrite(code, 1, codelen, stdout);
		putchar('\n');
		fflush(stdout);
		codelen = 0;
		if (once)
			break;
	}

	if (soft)
		ssi_send(tty, SSI_STOP, NULL, 0);
	tcdrain(tty);
	if (!keep)
		port_power(0);
	return ret;
}
