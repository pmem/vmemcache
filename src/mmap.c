// SPDX-License-Identifier: BSD-3-Clause
/* Copyright 2014-2019, Intel Corporation */

/*
 * mmap.c -- mmap utilities
 */

#include <errno.h>
#include <inttypes.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

#include "file.h"
#include "mmap.h"
#include "sys_util.h"
#include "os.h"

/*
 * util_map -- memory map a file
 *
 * This is just a convenience function that calls mmap() with the
 * appropriate arguments and includes our trace points.
 */
void *
util_map(int fd, size_t len, int flags, int rdonly, size_t req_align,
	int *map_sync)
{
	LOG(3, "fd %d len %zu flags %d rdonly %d req_align %zu map_sync %p",
			fd, len, flags, rdonly, req_align, map_sync);

	void *base;
	void *addr = util_map_hint(len, req_align);
	if (addr == MAP_FAILED) {
		ERR("cannot find a contiguous region of given size");
		return NULL;
	}

	if (req_align)
		ASSERTeq((uintptr_t)addr % req_align, 0);

	int proto = rdonly ? PROT_READ : PROT_READ|PROT_WRITE;
	base = util_map_sync(addr, len, proto, flags, fd, 0, map_sync);
	if (base == MAP_FAILED) {
		ERR("!mmap %zu bytes", len);
		return NULL;
	}

	LOG(3, "mapped at %p", base);

	return base;
}

/*
 * util_unmap -- unmap a file
 *
 * This is just a convenience function that calls munmap() with the
 * appropriate arguments and includes our trace points.
 */
int
util_unmap(void *addr, size_t len)
{
	LOG(3, "addr %p len %zu", addr, len);

/*
 * XXX Workaround for https://bugs.freebsd.org/bugzilla/show_bug.cgi?id=169608
 */
#ifdef __FreeBSD__
	if (!IS_PAGE_ALIGNED((uintptr_t)addr)) {
		errno = EINVAL;
		ERR("!munmap");
		return -1;
	}
#endif
	int retval = munmap(addr, len);
	if (retval < 0)
		ERR("!munmap");

	return retval;
}

/*
 * chattr -- (internal) set file attributes
 */
static void
chattr(int fd, int set, int clear)
{
	int attr;

	if (ioctl(fd, FS_IOC_GETFLAGS, &attr) < 0) {
		LOG(3, "!ioctl(FS_IOC_GETFLAGS) failed");
		return;
	}

	attr |= set;
	attr &= ~clear;

	if (ioctl(fd, FS_IOC_SETFLAGS, &attr) < 0) {
		LOG(3, "!ioctl(FS_IOC_SETFLAGS) failed");
		return;
	}
}

/*
 * util_map_tmpfile -- reserve space in an unlinked file and memory-map it
 *
 * size must be multiple of page size.
 */
void *
util_map_tmpfile(const char *dir, size_t size, size_t req_align)
{
	int oerrno;

	if (((os_off_t)size) < 0) {
		ERR("invalid size (%zu) for os_off_t", size);
		errno = EFBIG;
		return NULL;
	}

	int fd = util_tmpfile(dir, OS_DIR_SEP_STR "vmem.XXXXXX", O_EXCL);
	if (fd == -1) {
		LOG(2, "cannot create temporary file in dir %s", dir);
		goto err;
	}

	chattr(fd, FS_NOCOW_FL, 0);

	if ((errno = os_posix_fallocate(fd, 0, (os_off_t)size)) != 0) {
		ERR("!posix_fallocate");
		goto err;
	}

	void *base;
	if ((base = util_map(fd, size, MAP_SHARED,
			0, req_align, NULL)) == NULL) {
		LOG(2, "cannot mmap temporary file");
		goto err;
	}

	(void) os_close(fd);
	return base;

err:
	oerrno = errno;
	if (fd != -1)
		(void) os_close(fd);
	errno = oerrno;
	return NULL;
}
