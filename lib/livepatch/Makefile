# SPDX-License-Identifier: GPL-2.0
#
# Makefile for livepatch test code.

LIVEPATCH_test_klp_atomic_replace := y
LIVEPATCH_test_klp_callbacks_demo := y
LIVEPATCH_test_klp_callbacks_demo2 := y
LIVEPATCH_test_klp_convert1 := y
LIVEPATCH_test_klp_convert2 := y
LIVEPATCH_test_klp_livepatch := y

obj-$(CONFIG_TEST_LIVEPATCH) += test_klp_atomic_replace.o \
				test_klp_callbacks_demo.o \
				test_klp_callbacks_demo2.o \
				test_klp_callbacks_busy.o \
				test_klp_callbacks_mod.o \
				test_klp_convert1.o \
				test_klp_convert2.o \
				test_klp_convert_mod.o \
				test_klp_livepatch.o \
				test_klp_shadow_vars.o

test_klp_convert_mod-y := \
	test_klp_convert_mod_a.o \
	test_klp_convert_mod_b.o

# Target modules to be livepatched require CC_FLAGS_FTRACE
CFLAGS_test_klp_callbacks_busy.o	+= $(CC_FLAGS_FTRACE)
CFLAGS_test_klp_callbacks_mod.o		+= $(CC_FLAGS_FTRACE)
CFLAGS_test_klp_convert_mod.o		+= $(CC_FLAGS_FTRACE)
