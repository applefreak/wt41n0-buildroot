// SPDX-License-Identifier: GPL-2.0
/*
 * wt41n0-growfs: grow the mounted ext4 file system at a mount point to the size of
 * its block device, online, through the kernel (EXT4_IOC_RESIZE_FS). Used on the
 * first boot of the eMMC system, whose root file system image is smaller than its
 * partition.
 *
 *   wt41n0-growfs [mount point]      (default /)
 *
 * Exits 0 when the file system already fills the device.
 */
#include <endian.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#ifndef EXT4_IOC_RESIZE_FS
#define EXT4_IOC_RESIZE_FS _IOW('f', 16, uint64_t)
#endif

/*
 * The file system's size in blocks, from the superblock on the device. (statvfs's
 * f_blocks leaves out the metadata overhead, so it cannot tell whether the file
 * system already fills the device.)
 */
static int fs_blocks(unsigned int maj, unsigned int min, uint64_t *count)
{
	unsigned char sb[1024];
	char path[64], line[64], dev[80] = "";
	uint32_t lo, hi = 0, incompat;
	FILE *f;
	int fd, ok;

	snprintf(path, sizeof(path), "/sys/dev/block/%u:%u/uevent", maj, min);
	f = fopen(path, "r");
	if (!f)
		return -1;
	while (fgets(line, sizeof(line), f))
		if (!strncmp(line, "DEVNAME=", 8))
			snprintf(dev, sizeof(dev), "/dev/%.*s", (int)strcspn(line + 8, "\n"), line + 8);
	fclose(f);
	fd = open(dev, O_RDONLY);
	if (fd < 0)
		return -1;
	ok = pread(fd, sb, sizeof(sb), 1024) == sizeof(sb);
	close(fd);
	if (!ok || sb[0x38] != 0x53 || sb[0x39] != 0xEF)
		return -1;
	memcpy(&lo, sb + 0x04, 4);
	memcpy(&incompat, sb + 0x60, 4);
	if (le32toh(incompat) & 0x80)	/* 64bit */
		memcpy(&hi, sb + 0x150, 4);
	*count = (uint64_t)le32toh(hi) << 32 | le32toh(lo);
	return 0;
}

int main(int argc, char **argv)
{
	const char *mnt = argc > 1 ? argv[1] : "/";
	char path[64];
	struct statvfs vfs;
	struct stat st;
	unsigned long long sectors;
	uint64_t dev_bytes, blocks, cur;
	FILE *f;
	int fd;

	if (stat(mnt, &st) || statvfs(mnt, &vfs)) {
		perror(mnt);
		return 1;
	}
	/* size of the block device behind the mount point, in 512-byte sectors */
	snprintf(path, sizeof(path), "/sys/dev/block/%u:%u/size", major(st.st_dev), minor(st.st_dev));
	f = fopen(path, "r");
	if (!f || fscanf(f, "%llu", &sectors) != 1) {
		perror(path);
		return 1;
	}
	fclose(f);
	dev_bytes = (uint64_t)sectors * 512;

	blocks = dev_bytes / vfs.f_frsize;
	if (fs_blocks(major(st.st_dev), minor(st.st_dev), &cur)) {
		fprintf(stderr, "%s: cannot read the ext4 superblock\n", mnt);
		return 1;
	}
	if (blocks <= cur) {
		printf("%s: file system already fills its partition\n", mnt);
		return 0;
	}
	fd = open(mnt, O_RDONLY | O_DIRECTORY);
	if (fd < 0 || ioctl(fd, EXT4_IOC_RESIZE_FS, &blocks)) {
		perror("EXT4_IOC_RESIZE_FS");
		return 1;
	}
	printf("%s: grown from %llu to %llu blocks of %lu bytes\n", mnt,
	       (unsigned long long)cur, (unsigned long long)blocks, vfs.f_frsize);
	return 0;
}
