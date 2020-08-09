#!/usr/bin/env perl
# SPDX-License-Identifier: GPL-2.0-only
# Author: Masahiro Yamada <masahiroy@kernel.org>

use autodie;
use strict;
use warnings;
use Getopt::Long 'GetOptions';

my $ar = "ar";
my $output = "compile_commands.json";

GetOptions(
	'a|ar=s' => \$ar,
	'o|output=s'  => \$output,
);

# Collect all objects compiled for vmlinux and modules
my @objects;

foreach (@ARGV) {
	if (/\.o$/) {
		# Some objects (head-y) are linked to vmlinux directly.
		push(@objects, $_);
	} elsif (/\.a$/) {
		# Most of built-in objects are contained in built-in.a or lib.a.
		# You can use 'ar -t' to print the contained objects.
		$_ = `$ar -t $_`;
		push(@objects, split(/\n/));
	} elsif (/modules\.order$/) {
		# 'modules.order' lists all the modules.
		open(my $ko_fh, '<', "$_");
		while (<$ko_fh>) {
			chomp;
			s/ko$/mod/;
			# The first line of '*.mod' lists the objects that
			# compose the module.
			open(my $mod_fh, '<', "$_");
			$_ = <$mod_fh>;
			close $mod_fh;
			chomp;
			push(@objects, split(/ /));
		}
		close $ko_fh;
	} else {
		die "$_: unknown file type\n";
	}
}

open(my $out_fh, '>', "$output");
print $out_fh "[\n";

foreach (@objects) {
	# The command for foo/bar/baz.o is output to foo/bar/.baz.o.json
	s:([^/]*$):.$1.json:;
	if (! -e $_) {
		# *.json files are generated only for C files. If this *.json
		# does not exist, it was probably built from assembly. Skip it.
		next;
	}

	open(my $in_fh, '<', "$_");
	# Concatenate all the *.json files into compile_commands.json
	print $out_fh do { local $/; <$in_fh> };
	close $in_fh;
}

print $out_fh "]\n";
close $out_fh;
