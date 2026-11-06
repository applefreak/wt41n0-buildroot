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

int main(int argc, char **argv)
{
	const char *mnt = argc > 1 ? argv[1] : "/";
	char path[64];
	struct statvfs vfs;
	struct stat st;
	unsigned long long sectors;
	uint64_t dev_bytes, blocks;
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
	if (blocks <= vfs.f_blocks) {
		printf("%s: file system already fills its partition\n", mnt);
		return 0;
	}
	fd = open(mnt, O_RDONLY | O_DIRECTORY);
	if (fd < 0 || ioctl(fd, EXT4_IOC_RESIZE_FS, &blocks)) {
		perror("EXT4_IOC_RESIZE_FS");
		return 1;
	}
	printf("%s: grown from %llu to %llu blocks of %lu bytes\n", mnt,
	       (unsigned long long)vfs.f_blocks, (unsigned long long)blocks, vfs.f_frsize);
	return 0;
}
