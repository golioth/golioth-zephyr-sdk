# Copyright (c) 2022-2023 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

import logging
import os

from golioth import Client, Device
import pytest
import trio


# This is the same as using the @pytest.mark.anyio on all test functions in the module
pytestmark = pytest.mark.anyio


DEFAULT_TIMEOUT = 30


@pytest.fixture(scope='session')
async def device():
    client = Client(os.environ.get("GOLIOTHCTL_CONFIG"))
    project = await client.default_project()
    return await project.device_by_name(os.environ['GOLIOTH_DEVICE_NAME'])


async def counter_set(device: Device, value: int):
    logging.info("Setting /counter to %s", value)
    await device.lightdb.set('counter', value)


async def test_lightdb_counter_delete(initial_timeout, device):
    magic_value = 15664155
    counter = None

    async with device.lightdb.monitor('counter') as lightdb:
        # Allow much more time before first log entry arrives
        timeout = initial_timeout

        for _ in range(0, 5):
            with trio.fail_after(timeout):
                counter = await lightdb.get()

            # Once we are connected (log entries arrived), reduce timeout value
            timeout = DEFAULT_TIMEOUT

            logging.info("Updated counter value: %s (expected %s)", counter, magic_value)

            if counter == magic_value:
                break

            # Workaround LightDB issue, which does not consume requests too often
            await trio.sleep(1)

            await counter_set(device, magic_value)

        assert counter == magic_value, "Did not receive magic value"

        for _ in range(0, 3):
            with trio.fail_after(timeout):
                counter = await lightdb.get()

            # Once we are connected (log entries arrived), reduce timeout value
            timeout = DEFAULT_TIMEOUT

            logging.info("Updated counter value: %s (expected %s)", counter, None)

            if counter is None:
                logging.info("Received 'None'")
                break

        assert counter is None, "Did not receive 'None' value as expected"
