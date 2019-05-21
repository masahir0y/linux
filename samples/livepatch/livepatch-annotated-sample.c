/*
 * livepatch-annotated-sample.c - Kernel Live Patching Sample Module
 *
 * Copyright (C) 2014 Seth Jennings <sjenning@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/livepatch.h>

/*
 * This (dumb) live patch overrides the function that prints the
 * kernel boot cmdline when /proc/cmdline is read.
 *
 * This livepatch uses the symbol saved_command_line whose relocation
 * must be resolved during load time. To enable that, this module
 * must be post-processed by a tool called klp-convert, which embeds
 * information to be used by the loader to solve the relocation.
 *
 * The module is annotated with KLP_MODULE_RELOC/KLP_SYMPOS macros.
 * These annotations are used by klp-convert to infer that the symbol
 * saved_command_line is in the object vmlinux.
 *
 * As saved_command_line has no other homonimous symbol across
 * kernel objects, this annotation is not a requirement, and can be
 * suppressed with no harm to klp-convert. Yet, it is kept here as an
 * example on how to annotate livepatch modules that contain symbols
 * whose names are used in more than one kernel object.
 *
 * Example:
 *
 * $ cat /proc/cmdline
 * <your cmdline>
 *
 * $ insmod livepatch-sample.ko
 * $ cat /proc/cmdline
 * <your cmdline> livepatch=1
 *
 * $ echo 0 > /sys/kernel/livepatch/livepatch_sample/enabled
 * $ cat /proc/cmdline
 * <your cmdline>
 */

extern char *saved_command_line;

#include <linux/seq_file.h>
static int livepatch_cmdline_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%s livepatch=1\n", saved_command_line);
	return 0;
}

KLP_MODULE_RELOC(vmlinux) vmlinux_relocs[] = {
	KLP_SYMPOS(saved_command_line, 0)
};

static struct klp_func funcs[] = {
	{
		.old_name = "cmdline_proc_show",
		.new_func = livepatch_cmdline_proc_show,
	}, { }
};

static struct klp_object objs[] = {
	{
		/* name being NULL means vmlinux */
		.funcs = funcs,
	}, { }
};

static struct klp_patch patch = {
	.mod = THIS_MODULE,
	.objs = objs,
};

static int livepatch_init(void)
{
	return klp_enable_patch(&patch);
}

static void livepatch_exit(void)
{
}

module_init(livepatch_init);
module_exit(livepatch_exit);
MODULE_LICENSE("GPL");
