# Copyright (c) 2022 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

import os
import sys
from pathlib import Path
from datetime import datetime, timedelta
import logging

from golioth import Client, RPCTimeout
import pytest
from trio import sleep


# This is the same as using the @pytest.mark.anyio on all test functions in the module
pytestmark = pytest.mark.anyio


DEFAULT_TIMEOUT = 30


@pytest.fixture(scope='session')
def initial_timeout(request):
    timeout = request.config.getoption('--initial-timeout')
    if timeout is None:
        timeout = DEFAULT_TIMEOUT
    return timeout


@pytest.fixture(scope='session')
async def device(initial_timeout):
    device_name = os.environ["GOLIOTH_DEVICE_NAME"]

    client = Client(os.environ.get("GOLIOTHCTL_CONFIG"))
    project = await client.default_project()
    device = await project.device_by_name(device_name)

    # Try to call method for the first time, just to make sure that device is already available
    # (already connected).
    start = datetime.now()

    while datetime.now() < start + timedelta(seconds=initial_timeout):
        try:
            logging.debug('Calling "multiply" to check if device is connected')
            await device.rpc.multiply(1, 1)

            # No exception was raised, so just return device
            return device
        except RPCTimeout:
            pass

        await sleep(1)

    raise RuntimeError('Timeout while waiting for first successful RPC call')


@pytest.mark.parametrize('a,b',
                         [(3, 5),
                          (12, 15),
                          (-7, 8),
                          (-78, -64)])
async def test_rpc_multiply(device, a, b):
    resp = await device.rpc.multiply(a, b)
    assert resp['value'] == a * b, "Invalid result"
