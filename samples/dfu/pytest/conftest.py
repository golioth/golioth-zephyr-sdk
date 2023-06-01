# Copyright (c) 2020 Intel Corporation.
# Copyright (c) 2022-2023 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import sys

import pytest

GOLIOTH_BASE = Path(__file__).resolve().parents[3]
ZEPHYR_BASE = GOLIOTH_BASE.parents[2] / 'zephyr'

sys.path.insert(0, str(ZEPHYR_BASE / 'scripts' / 'west_commands'))
from build_info import BuildInfo

@pytest.fixture(scope='session')
def anyio_backend():
    return 'trio'

@pytest.fixture(scope='session')
def build_info(request):
    return BuildInfo(Path(request.config.option.build_dir))

@pytest.fixture(scope='session')
def initial_timeout(build_info: BuildInfo):
    return build_info.conf['CONFIG_GOLIOTH_SAMPLE_TEST_CONNECT_TIMEOUT']
