#!/bin/sh
#
# binutils-version [-p] command
#
# Prints the binutils version of `command' in a canonical 4-digit form
# such as `0224' for binutils 2.24.
#
# With the -p option, prints the patchlevel as well, for example `022400' for
# binutils 2.24.0.
#

if [ "$1" = "-p" ] ; then
	with_patchlevel=1;
	shift;
fi

prog="$*"

if [ ${#prog} -eq 0 ]; then
	echo "Error: No program specified."
	printf "Usage:\n\t$0 <command>\n"
	exit 1
fi

version_string=$($prog --version | head -1 | sed -e 's/.*) *\([0-9.]*\).*/\1/' )

MAJOR=$(echo $version_string | cut -d . -f 1)
MINOR=$(echo $version_string | cut -d . -f 2)
if [ "x$with_patchlevel" != "x" ] ; then
	PATCHLEVEL=$(echo $version_string | cut -d . -f 3)
	printf "%02d%02d%02d\\n" $MAJOR $MINOR $PATCHLEVEL
else
	printf "%02d%02d\\n" $MAJOR $MINOR
fi
