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


async def test_stream_events_received(initial_timeout, device):
    async with device.stream.monitor() as stream:
        # Allow much more time before first log entry arrives
        timeout = initial_timeout

        for i in range(0, 5):
            with trio.fail_after(timeout):
                entry = await stream.get()

            # Once we are connected (stream entries arrived), reduce timeout value
            timeout = DEFAULT_TIMEOUT

            assert "timestamp" in entry, "No 'timestamp' in stream"
            ts = entry["timestamp"]

            assert "data" in entry, "No 'data' in stream"
            assert "temp" in entry["data"], "No 'temp' in stream data"

            temp = entry["data"]["temp"]
            assert isinstance(temp, (int, float)), f"Temp ({temp}) is not a number"
            assert 0 < temp and temp < 80, "Temperature value is insane"

            logging.info("Temperature %-5s at %s", temp, ts)
