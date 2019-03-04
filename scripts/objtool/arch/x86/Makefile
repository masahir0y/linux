# SPDX-License-Identifier: GPL-2.0

objtool-objs += arch/$(SRCARCH)/decode.o

$(obj)/arch/$(SRCARCH)/decode.o: $(obj)/arch/$(SRCARCH)/inat-table.c

inat_tables_script = $(srctree)/$(src)/arch/$(SRCARCH)/tools/gen-insn-attr-x86.awk
inat_tables_maps = $(srctree)/$(src)/arch/$(SRCARCH)/lib/x86-opcode-map.txt

quiet_cmd_inat_table = AWK      $@
      cmd_inat_table = $(AWK) -f $(inat_tables_script) $(inat_tables_maps) > $@

$(obj)/arch/$(SRCARCH)/inat-table.c: $(inat_tables_script) $(inat_tables_maps)
	$(call cmd,inat_table)

clean-files += arch/$(SRCARCH)/inat-table.c
