/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __LINUX_VERSION_H__
#define __LINUX_VERSION_H__

#include <linux/version-field.h>

#define KERNEL_VERSION(a, b, c)	(((a) << 24) + ((b) << 16) + (c))

#define LINUX_VERSION_CODE	KERNEL_VERSION(LINUX_VERSION_MAJOR, \
					       LINUX_VERSION_PATCHLEVEL, \
					       LINUX_VERSION_SUBLEVEL)

#endif /* __LINUX_VERSION_H__ */
