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

@pytest.fixture(scope='session')
def initial_timeout(request):
    build_conf = BuildConfiguration(request.config.option.build_dir)

    return build_conf['CONFIG_GOLIOTH_SAMPLE_TEST_CONNECT_TIMEOUT']
