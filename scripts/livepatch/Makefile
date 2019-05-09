hostprogs-y			:= klp-convert
always				:= $(hostprogs-y)

klp-convert-objs		:= klp-convert.o elf.o

HOST_EXTRACFLAGS		:= -g -I$(INSTALL_HDR_PATH)/include -Wall
HOSTLDLIBS_klp-convert		:= -lelf
