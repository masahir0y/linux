#!/usr/bin/env python
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) Google LLC, 2018
#
# Author: Tom Roeder <tmroeder@google.com>
#
"""A tool for generating compile_commands.json in the Linux kernel."""

import argparse
import json
import logging
import os
import re
import subprocess

_DEFAULT_OUTPUT = 'compile_commands.json'
_DEFAULT_LOG_LEVEL = 'WARNING'

_FILENAME_PATTERN = r'^\..*\.cmd$'
_LINE_PATTERN = r'^cmd_[^ ]*\.o := (.* )([^ ]*\.c)$'
_VALID_LOG_LEVELS = ['DEBUG', 'INFO', 'WARNING', 'ERROR', 'CRITICAL']

# A kernel build generally has over 2000 entries in its compile_commands.json
# database. If this code finds 300 or fewer, then warn the user that they might
# not have all the .cmd files, and they might need to compile the kernel.
_LOW_COUNT_THRESHOLD = 300


def parse_arguments():
    """Sets up and parses command-line arguments.

    Returns:
        log_level: A logging level to filter log output.
        directory: The directory to search for .cmd files.
        output: Where to write the compile-commands JSON file.
    """
    usage = 'Creates a compile_commands.json database from kernel .cmd files'
    parser = argparse.ArgumentParser(description=usage)

    output_help = ('The location to write compile_commands.json (defaults to '
                   'compile_commands.json in the search directory)')
    parser.add_argument('-o', '--output', type=str, help=output_help)

    log_level_help = ('The level of log messages to produce (one of ' +
                      ', '.join(_VALID_LOG_LEVELS) + '; defaults to ' +
                      _DEFAULT_LOG_LEVEL + ')')
    parser.add_argument(
        '--log_level', type=str, default=_DEFAULT_LOG_LEVEL,
        help=log_level_help)

    parser.add_argument('files', nargs='*', help="")

    args = parser.parse_args()

    log_level = args.log_level
    if log_level not in _VALID_LOG_LEVELS:
        raise ValueError('%s is not a valid log level' % log_level)

    output = args.output or os.path.join(directory, _DEFAULT_OUTPUT)
    files = args.files

    return log_level, output, files


def process_line(root_directory, command_prefix, relative_path):
    """Extracts information from a .cmd line and creates an entry from it.

    Args:
        root_directory: The directory that was searched for .cmd files. Usually
            used directly in the "directory" entry in compile_commands.json.
        file_directory: The path to the directory the .cmd file was found in.
        command_prefix: The extracted command line, up to the last element.
        relative_path: The .c file from the end of the extracted command.
            Usually relative to root_directory, but sometimes relative to
            file_directory and sometimes neither.

    Returns:
        An entry to append to compile_commands.

    Raises:
        ValueError: Could not find the extracted file based on relative_path and
            root_directory or file_directory.
    """
    # The .cmd files are intended to be included directly by Make, so they
    # escape the pound sign '#', either as '\#' or '$(pound)' (depending on the
    # kernel version). The compile_commands.json file is not interepreted
    # by Make, so this code replaces the escaped version with '#'.
    prefix = command_prefix.replace('\#', '#').replace('$(pound)', '#')

    cur_dir = root_directory
    expected_path = os.path.join(cur_dir, relative_path)
    if not os.path.exists(expected_path):
        raise ValueError('expectiveFile %s not in %s or %s' %
                         (relative_path, root_directory, file_directory))
    return {
        'directory': cur_dir,
        'file': relative_path,
        'command': prefix + relative_path,
    }

def main():

    log_level, output, files = parse_arguments()

    level = getattr(logging, log_level)
    logging.basicConfig(format='%(levelname)s: %(message)s', level=level)

    line_matcher = re.compile(_LINE_PATTERN)

    objects = []
    for file in files:
        if file.endswith(".o"):
            objects.append(file)
        elif file.endswith(".a"):
            objects += subprocess.check_output(['ar', '-t', file]).decode().split()
        elif file.endswith(".order"):
            for line in open(file):
                ko = line.rstrip()
                base, ext = os.path.splitext(ko)
                if ext != ".ko":
                    sys.exit("{}: mobule path must end with .ko".format(ko))
                mod = base + ".mod"
                with open(mod) as f:
                    objects += f.readline().split()

    compile_commands = []
    directory = os.getcwd()
    for object in objects:
        dir, notdir = os.path.split(object)
        if not notdir.endswith(".o"):
            sys.exit("{}: object path must end with .o".format(object))
        cmd_file = os.path.join(dir, "." + notdir + ".cmd")
        with open(cmd_file, 'rt') as f:
            for line in f:
                result = line_matcher.match(line)
                if not result:
                    continue

                try:
                    entry = process_line(directory, result.group(1), result.group(2))
                    compile_commands.append(entry)
                except ValueError as err:
                    logging.info('Could not add line from %s: %s',
                                 filepath, err)

    line_matcher = re.compile(_LINE_PATTERN)

    with open(output, 'wt') as f:
        json.dump(compile_commands, f, indent=2, sort_keys=True)

if __name__ == '__main__':
    main()
