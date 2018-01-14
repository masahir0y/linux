# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) 2018 Masahiro Yamada <yamada.masahiro@socionext.com>
#

import os
import pytest
import subprocess

class Conf:

    def __init__(self):
        pass

    def run_cmd2(self, mode, kconfig, interactive):
        ps = subprocess.Popen(['scripts/kconfig/conf', '--' + mode,
                               'scripts/kconfig/tests/' + kconfig],
                              stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                              env=dict(os.environ, KCONFIG_CONFIG='/tmp/hoge'))

        if interactive:
            while ps.poll() == None:
                ps.stdin.write(b'\n')

        stdout = ps.stdout.read().decode()

        with open('/tmp/hoge') as f:
            dot_config = f.readlines()

        return (dot_config, stdout)

    def run_cmd_interactive(self, mode, kconfig):
        subprocess.call(['scripts/kconfig/conf', '--' + mode,
                         'scripts/kconfig/tests/' + kconfig],
                        env=dict(os.environ, KCONFIG_CONFIG='/tmp/hoge'))


    def run_cmd(self, mode, kconfig):
        subprocess.call(['scripts/kconfig/conf', '--' + mode,
                         'scripts/kconfig/tests/' + kconfig],
                        env=dict(os.environ, KCONFIG_CONFIG='/tmp/hoge'))

    def oldaskconfig(self, kconfig):
        return self.run_cmd2('oldaskconfig', kconfig, 1)

    def alldefconfig(self, kconfig):
        return self.run_cmd2('alldefconfig', kconfig, 0)

@pytest.fixture(scope="module")
def conf():
    return Conf()
