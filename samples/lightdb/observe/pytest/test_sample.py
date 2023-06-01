# Copyright (c) 2022-2023 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

import base64
import logging
import os

from golioth import Client, Device
import pytest
import trio

from twister_harness.device.device_abstract import DeviceAbstract


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


async def test_lightdb_counter_observe(initial_timeout, device, dut: DeviceAbstract):
    magic_value = 8664100
    expected_updates = 5

    async with device.logs_monitor() as logs_monitor:
        await trio.sleep(2) # Wait 2s to be (almost) sure that logs will show updates counter
        counter_expected = magic_value
        await counter_set(device, counter_expected - 1)

        # Workaround LightDB issue, which does not consume requests too often
        await trio.sleep(1)

        await counter_set(device, counter_expected)

        # Allow much more time before first log entry arrives
        timeout = initial_timeout

        while magic_value + expected_updates > counter_expected:
            with trio.fail_after(timeout):
                log = await logs_monitor.get()

            # Once we are connected (log entries arrived), reduce timeout value
            timeout = DEFAULT_TIMEOUT

            if "Counter" not in log.message:
                continue

            binary_value = base64.decodebytes(log.metadata["hexdump"].encode())
            counter_logged = int(binary_value.decode())

            logging.info("Logged %s (expected %s)", counter_logged, counter_expected)

            if counter_logged == counter_expected:
                counter_expected += 1

                # Workaround LightDB issue, which does not consume requests too often
                await trio.sleep(1)

                await counter_set(device, counter_expected)
