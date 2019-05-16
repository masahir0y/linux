#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

set -e

# Check uniqueness of module names
check_same_name_modules()
{
	for m in $(sed 's:.*/::' modules.order | sort | uniq -d)
	do
		echo "warning: same module names found:" >&2
		sed -n "/\/$m/s:^:  :p" modules.order >&2
	done
}

# Check MODULE_ macros in non-modular code
check_orphan_module_macros()
{
	# modules.builtin.modinfo is created while linking vmlinux.
	# It may not exist when you do 'make modules'.
	if [ ! -r modules.builtin.modinfo ]; then
		return
	fi

	# modules.builtin lists *real* built-in modules, i.e. controlled by
	# tristate CONFIG options, but currently built with =y.
	#
	# modules.builtin.modinfo is the list of MODULE_ macros compiled
	# into vmlinux.
	#
	# By diff'ing them, users of bogus MODULE_* macros will show up.

	# Kbuild replaces ',' and '-' in file names with '_' for use in C.
	real_builtin_modules=$(sed -e 's:.*/::' -e 's/\.ko$//' -e 's/,/_/g' \
			       -e 's/-/_/g' modules.builtin | sort | uniq)

	show_hint=

	# Exclude '.paramtype=' and '.param=' to skip checking module_param()
	# and MODULE_PARM_DESC().
	module_macro_users=$(tr '\0' '\n' < modules.builtin.modinfo |
			     sed -e '/\.parmtype=/d' -e '/\.parm=/d' |
			     sed -n 's/\..*//p' | sort | uniq)

	for m in $module_macro_users
	do
		warn=1

		for n in $real_builtin_modules
		do
			if [ "$m" = "$n" ]; then
				warn=
				break
			fi
		done

		if [ -n "$warn" ]; then
			echo "notice: $m: MODULE macros found in non-modular code"
			show_hint=1
		fi
	done

	if [ -n "$show_hint" ]; then
		echo " To fix above, check MODULE_LICENSE(), MODULE_AUTHOR(), etc."
		echo " Please check #include <linux/module.h>, THIS_MODULE, too."
	fi
}

check_same_name_modules
check_orphan_module_macros
