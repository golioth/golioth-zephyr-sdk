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


async def test_lightdb_counter_received(initial_timeout, device):
    expected_updates = 5

    async with device.lightdb.monitor('counter') as lightdb:
        # Allow much more time before first log entry arrives
        timeout = initial_timeout

        with trio.fail_after(timeout):
            counter = await lightdb.get()

        logging.info("Ignoring initial value %s", counter)

        counters = set()

        for i in range(0, expected_updates + 1):
            with trio.fail_after(timeout):
                counter = await lightdb.get()

            # Once we are connected (log entries arrived), reduce timeout value
            timeout = DEFAULT_TIMEOUT

            counters.add(counter)

            logging.info("Counter updated to %s", counter)

    for i in range(1, expected_updates + 1):
        assert i in counters, f"No counter value {i} in collected counters"
