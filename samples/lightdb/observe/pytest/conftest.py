# Copyright (c) 2020 Intel Corporation.
# Copyright (c) 2022-2023 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import sys

import pytest

GOLIOTH_BASE = Path(__file__).resolve().parents[4]
ZEPHYR_BASE = GOLIOTH_BASE.parents[2] / 'zephyr'

sys.path.insert(0, str(ZEPHYR_BASE / 'scripts' / 'west_commands'))
from runners.core import BuildConfiguration

@pytest.fixture(scope='session')
def anyio_backend():
    return 'trio'

# add option "--comdopt" to pytest, or it will report "unknown option"
# this option is passed from twister.
def pytest_addoption(parser):
    parser.addoption(
        '--cmdopt'
    )

# define fixture to return value of option "--cmdopt", this fixture
# will be requested by other fixture of tests.
@pytest.fixture(scope='session')
def cmdopt(request):
    return request.config.getoption('--cmdopt')

@pytest.fixture(scope='session')
def initial_timeout(cmdopt):
    build_conf = BuildConfiguration(cmdopt)

    return build_conf['CONFIG_GOLIOTH_SAMPLE_TEST_CONNECT_TIMEOUT']
