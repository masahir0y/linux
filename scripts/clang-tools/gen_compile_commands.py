#!/bin/python

import argparse
import os
import subprocess
import sys

def gen_compile_commands(dst, objects):
    for object in objects:
        dir, notdir = os.path.split(object)
        if not notdir.endswith(".o"):
            sys.exit("{}: object path must end with .o".format(object))
        json = os.path.join(dir, "." + notdir + ".json")
        if os.path.isfile(json):
            with open(json) as f:
                dst.write(f.read())

def main():
    usage = 'Creates a compile_commands.json database'
    parser = argparse.ArgumentParser(description=usage)

    parser.add_argument('-o', '--output', type=str, help="")

    parser.add_argument('files', nargs='*', help="")

    args = parser.parse_args()

    with open(args.output, 'w') as dst:

        for f in args.files:
            if f.endswith(".o"):
                gen_compile_commands(dst, [f])
            elif f.endswith(".a"):
                gen_compile_commands(dst, subprocess.check_output(['ar', '-t', f]).decode().split())
            elif f.endswith(".order"):
                for line in open(f):
                    ko = line.rstrip()
                    base, ext = os.path.splitext(ko)
                    if ext != ".ko":
                        sys.exit("{}: mobule path must end with .ko".format(ko))
                    mod = base + ".mod"
                    with open(mod) as g:
                        gen_compile_commands(dst, g.readline().split())

if __name__ == '__main__':
    main()
