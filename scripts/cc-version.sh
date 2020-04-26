#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# Print the compiler version in a 5 or 6-digit form. Works for gcc and clang.

if [ $# = 0 ]; then
	echo "Usage: $0 <compiler-command>" >&2
	exit 1
fi

cat <<EOF | $@ -E -P -x assembler-with-cpp - | awk 'END {printf $1 * 10000 + $2 * 100 + $3}' -
#if defined(__clang__)
__clang_major__  __clang_minor__ __clang_patchlevel__
#elif defined(__GNUC__)
__GNUC__ __GNUC_MINOR__ __GNUC_PATCHLEVEL__
#else
#warning "unknown compiler"
0 0 0
#endif
EOF
