#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

set -e

# Check uniqueness of module names
check_same_name_modules()
{
	for m in $(sed 's:.*/::' modules.order | sort | uniq -d)
	do
		echo "warning: same module names found:" >&2
		sed -n "/\/$m/s:^kernel/:  :p" modules.order >&2
	done
}

# Check MODULE_ macros in non-modular code
check_bogus_module_macros()
{
	if [ ! -r modules.builtin.modinfo ]; then
		return
	fi

	# modules.builtin lists *real* built-in modules. In other words,
	# controlled by tristate CONFIG options, but currently built with =y.
	#
	# On the other hand, modules.builtin.modinfo is the list of MODULE_
	# macros compiled into vmlinux, including ones from
	# never-compiled-as-module code.
	#
	# By diff'ing them, users of bogus MODULE_* macros will show up.

	# Kbuild replaces ',' and '-' in file names with '_' for use in C
	real_builtin_modules=$(cat modules.builtin | \
				xargs basename -s .ko -- 2>/dev/null | \
				tr ',-' '__' | sort | uniq)

	show_fix=

	# Exclude '.paramtype=' because module_param() is legitimately used by
	# non-modular code. Exclude '.param='; do not check MODULE_PARAM_DESC()
	# because it might be useful.
	module_macro_users=$(tr '\0' '\n' < modules.builtin.modinfo | \
		sed -e '/\.parmtype=/d' -e '/\.parm=/d' | \
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
			show_fix=1
		fi
	done

	if [ -n "$show_fix" ]; then
		echo "  To fix above, check bogus MODULE_LICENSE(), MODULE_AUTHOR(), etc."
		echo "  Pleae check #include <linux/module.h>, THIS_MODULE, too."
	fi
}

check_same_name_modules
check_bogus_module_macros
