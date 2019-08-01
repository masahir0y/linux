# SPDX-License-Identifier: GPL-2.0

ccflags-y				+= -I $(srctree)/$(src) -I $(srctree)/$(src)/$(GVT_DIR)/

i915-y += \
	gvt.o \
	aperture_gm.o \
	handlers.o \
	vgpu.o \
	trace_points.o \
	firmware.o \
	interrupt.o \
	gtt.o \
	cfg_space.o \
	opregion.o \
	mmio.o \
	display.o \
	edid.o \
	execlist.o \
	scheduler.o \
	sched_policy.o \
	mmio_context.o \
	cmd_parser.o \
	debugfs.o \
	fb_decoder.o \
	dmabuf.o \
	page_track.o

obj-$(CONFIG_DRM_I915_GVT_KVMGT) += kvmgt.o
