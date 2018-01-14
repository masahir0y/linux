def test_choice_alldefconfig(conf):
    #run_conf2()
    (config, stdout) = conf.alldefconfig('Kconfig.choice')
    assert 1

def test_choice_oldaskconfig(conf):
    conf.oldaskconfig('Kconfig.choice')
    assert 1
