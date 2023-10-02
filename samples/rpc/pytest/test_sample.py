# Copyright (c) 2022-2023 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

import logging
import os
from typing import Generator

from golioth import Client, RPCTimeout
import pytest
from tenacity import AsyncRetrying, retry_if_exception_type, stop_after_delay, wait_fixed
from trio import sleep

from twister_harness.fixtures import DeviceAdapter


# This is the same as using the @pytest.mark.anyio on all test functions in the module
pytestmark = pytest.mark.anyio


@pytest.fixture(scope='session')
def dut(request: pytest.FixtureRequest, device_object: DeviceAdapter) -> Generator[DeviceAdapter, None, None]:
    """Return launched device - with run application."""
    test_name = request.node.name
    device_object.initialize_log_files(test_name)
    try:
        device_object.launch()
        yield device_object
    finally:  # to make sure we close all running processes execution
        device_object.close()


@pytest.fixture(scope='session')
async def device(initial_timeout, dut: DeviceAdapter):
    device_name = os.environ["GOLIOTH_DEVICE_NAME"]

    client = Client(os.environ.get("GOLIOTHCTL_CONFIG"))
    project = await client.default_project()
    device = await project.device_by_name(device_name)

    # Try to call method for the first time, just to make sure that device is already available
    # (already connected).
    async for attempt in AsyncRetrying(stop=stop_after_delay(initial_timeout),
                                       wait=wait_fixed(1),
                                       retry=retry_if_exception_type(RPCTimeout),
                                       sleep=sleep):
        with attempt:
            logging.debug('Calling "multiply" to check if device is connected')
            await device.rpc.multiply(1, 1)

            # No exception was raised, so just return device
            return device


@pytest.mark.parametrize('a,b',
                         [(3, 5),
                          (12, 15),
                          (-7, 8),
                          (-78, -64)])
async def test_rpc_multiply(device, a, b):
    resp = await device.rpc.multiply(a, b)
    assert resp['value'] == a * b, "Invalid result"
