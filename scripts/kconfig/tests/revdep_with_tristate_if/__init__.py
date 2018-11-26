# SPDX-License-Identifier: GPL-2.0
"""
select/imply property with tristate if-conditional

The reverse dependencies (select/imply) are used to define a lower limit
of another symbol. The current value of the selector is set to the lower
limit of the selectee. This did not handled correctly in the past when the
property has a tristate if-conditional.
"""


def test(conf):
    assert conf.alldefconfig() == 0
    assert conf.config_matches('expected_config')
