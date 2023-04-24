# Copyright (c) 2022-2023 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

import logging
import os
import re
from typing import Iterable

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


async def leds_set(device: Device, leds: Iterable[bool]):
    logging.info("Setting /led to %s", leds)
    await device.lightdb.set('led', {i: led for i, led in enumerate(leds)})


async def test_lightdb_led(initial_timeout, device):
    num_leds = 4
    num_leds_on = 0
    msg_pattern = re.compile(r"LED (?P<id>\d+) -> (?P<state>O[NF]+)")
    leds = [False for i in range(num_leds)]

    async with device.logs_monitor() as logs_monitor:
        await trio.sleep(2) # Wait 2s to be (almost) sure that logs will show LED updates
        await leds_set(device, leds)

        # Allow much more time before first log entry arrives
        timeout = initial_timeout

        while True:
            with trio.fail_after(timeout):
                log = await logs_monitor.get()

            # Once we are connected (log entries arrived), reduce timeout value
            timeout = DEFAULT_TIMEOUT

            msg = msg_pattern.match(log.message)
            if not msg:
                continue

            leds[int(msg['id'])] = msg['state'] == 'ON'

            logging.info("Logged %s (expected %s)", leds,
                         [True] * num_leds_on + [False] * (num_leds - num_leds_on))

            if sum([leds[i] == True if i < num_leds_on else leds[i] == False
                    for i in range(num_leds)]) == num_leds:
                num_leds_on += 1
                if num_leds_on > num_leds:
                    break

                leds[num_leds_on - 1] = True

                # Workaround LightDB issue, which does not consume requests too often
                await trio.sleep(1)

                await leds_set(device, leds)
