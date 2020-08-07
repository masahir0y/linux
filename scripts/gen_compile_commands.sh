#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -e

echo_build_commands()
{
	# For allmodconfig, module.order contains too many objects.
	# Receive the list of objects from stdin instead of arguments
	# to avoid "Argument list too long" error.
	while read object
	do
		# The command for foo/bar/baz.o is output to foo/bar/.baz.o.json
		json=$(dirname $object)/.$(basename $object).json

		# .json files are generated only for C files.
		# If this .json file does not exist, it was probably built
		# from assembly. Ignore it.
		if [ -r $json ]; then
			cat $json
		fi
	done
}

echo '['

# Most of built-in objects are contained in built-in.a or lib.a
# Objects in head-y are linked directly.
#
# Modules are listed in modules.order
for f in $@
do
	case $f in
	*.o)
		echo $f;;
	*.a)
		$AR -t $f;;
	*.order)
		sed 's/ko$/mod/' $f | xargs -n 1 -- head -n 1  | tr ' ' '\n';;
	*)
		echo '$f: cannot handle this file' >&2; exit 1
	esac | echo_build_commands
done

echo ']'
