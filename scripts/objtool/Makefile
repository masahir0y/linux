# SPDX-License-Identifier: GPL-2.0

hostprogs-y := objtool
always := objtool

HOSTLDLIBS_objtool := -lelf

objtool-objs := \
	builtin-check.o \
	builtin-orc.o \
	check.o \
	orc_gen.o \
	orc_dump.o \
	elf.o \
	special.o \
	objtool.o \
	libstring.o \
	str_error_r.o \
	exec-cmd.o \
	pager.o \
	parse-options.o \
	run-command.o \
	sigchain.o \
	subcmd-config.o

HOST_EXTRACFLAGS := -I $(srctree)/tools/include -I $(srctree)/tools/lib -I $(srctree)/$(src)/arch/$(SRCARCH)/include

$(obj)/libstring.c: tools/lib/string.c
	$(call cmd,shipped)

$(obj)/str_error_r.c: tools/lib/str_error_r.c
	$(call cmd,shipped)

#$(obj)/parse-options.c: tools/lib/subcmd/parse-options.c
#	$(call cmd,shipped)

include $(srctree)/$(src)/arch/$(SRCARCH)/Makefile
