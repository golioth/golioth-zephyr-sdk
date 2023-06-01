# Copyright (c) 2022-2023 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

import logging
import os
from typing import Generator, Type

from golioth import Client, RPCTimeout
import pytest
from tenacity import AsyncRetrying, retry_if_exception_type, stop_after_delay, wait_fixed
from trio import sleep

from twister_harness.device.device_abstract import DeviceAbstract
from twister_harness.device.factory import DeviceFactory
from twister_harness.twister_harness_config import DeviceConfig, TwisterHarnessConfig


# This is the same as using the @pytest.mark.anyio on all test functions in the module
pytestmark = pytest.mark.anyio


@pytest.fixture(scope='session')
def dut(request: pytest.FixtureRequest) -> Generator[DeviceAbstract, None, None]:
    """Return device instance."""
    twister_harness_config: TwisterHarnessConfig = request.config.twister_harness_config  # type: ignore
    device_config: DeviceConfig = twister_harness_config.devices[0]
    device_type = device_config.type

    device_class: Type[DeviceAbstract] = DeviceFactory.get_device(device_type)

    device = device_class(device_config)

    try:
        device.generate_command()
        device.initialize_log_files()
        device.flash_and_run()
        device.connect()
        yield device
    except KeyboardInterrupt:
        pass
    finally:  # to make sure we close all running processes after user broke execution
        device.disconnect()
        device.stop()



@pytest.fixture(scope='session')
async def device(initial_timeout, dut: DeviceAbstract):
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
