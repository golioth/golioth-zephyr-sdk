# Copyright (c) 2022 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

from contextlib import suppress
import json
import logging
import os
import re
import subprocess
import time

import pexpect
import pytest


DEFAULT_TIMEOUT = 30


@pytest.fixture()
def initial_timeout(request):
    timeout = request.config.getoption('--initial-timeout')
    if timeout is None:
        timeout = DEFAULT_TIMEOUT
    return timeout


def device_name():
    return os.environ["GOLIOTH_DEVICE_NAME"]


def goliothctl_args():
    args = []

    with suppress(KeyError):
        args += ['-c', os.environ["GOLIOTHCTL_CONFIG"]]

    with suppress(KeyError):
        args += pexpect.split_command_line(os.environ["GOLIOTHCTL_OPTS"])

    return args


def leds_set(leds):
    value = json.dumps({str(i): v for i, v in enumerate(leds)})
    logging.info("Setting /led to %s", value)
    subprocess.run(["goliothctl"] + goliothctl_args() + ["lightdb", "set", device_name(), f"/led", "-b", value],
                   check=True)


def goliothctl_readline(goliothctl, timeout):
    index = goliothctl.expect([goliothctl.crlf, goliothctl.delimiter],
                              timeout=timeout)
    if index == 0:
        return goliothctl.before + goliothctl.crlf
    else:
        return goliothctl.before


def test_lightdb_led(initial_timeout):
    num_leds = 4
    num_leds_on = 0
    msg_pattern = re.compile(r"LED (\d+) -> (O[NF]+)")
    leds = [False for i in range(num_leds)]

    try:
        args = goliothctl_args() + ["logs", "listen", "--json", device_name()]

        logging.info("running goliothctl with args=%s", args)
        goliothctl = pexpect.spawn("goliothctl", args)

        time.sleep(2) # Wait 2s to be (almost) sure that logs will show LED updates
        leds_set(leds)

        # Allow much more time before first log entry arrives
        timeout = initial_timeout

        while True:
            line = goliothctl_readline(goliothctl, timeout)
            entry = json.loads(line)

            # Once we are connected (log entries arrived), reduce timeout value
            timeout = DEFAULT_TIMEOUT

            assert "message" in entry, f"No 'message' in {entry}"

            msg = msg_pattern.match(entry["message"])
            if not msg:
                continue

            led_id = int(msg[1])
            led_on = True if msg[2] == "ON" else False

            leds[led_id] = led_on

            logging.info("Logged %s (expected %s)", leds,
                         [True] * num_leds_on + [False] * (num_leds - num_leds_on))

            if sum([leds[i] == True if i < num_leds_on else leds[i] == False
                    for i in range(num_leds)]) == num_leds:
                num_leds_on += 1
                if num_leds_on > num_leds:
                    break

                leds[num_leds_on - 1] = True

                # Workaround LightDB issue, which does not consume requests too often
                time.sleep(1)

                leds_set(leds)
    finally:
        goliothctl.terminate()
