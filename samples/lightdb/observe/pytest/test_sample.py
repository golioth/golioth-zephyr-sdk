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
    return request.config.getoption('--initial-timeout', DEFAULT_TIMEOUT)


def device_name():
    return os.environ["GOLIOTH_DEVICE_NAME"]


def goliothctl_args():
    args = []

    with suppress(KeyError):
        args += pexpect.split_command_line(os.environ["GOLIOTHCTL_OPTS"])

    return args


def counter_set(value: int):
    logging.info("Setting /counter to %s", value)
    subprocess.run(["goliothctl"] + goliothctl_args() + ["lightdb", "set", device_name(), "/counter", "-b", str(value)],
                   check=True)


def goliothctl_readline(goliothctl, timeout):
    index = goliothctl.expect([goliothctl.crlf, goliothctl.delimiter],
                              timeout=timeout)
    if index == 0:
        return goliothctl.before + goliothctl.crlf
    else:
        return goliothctl.before


def test_lightdb_counter_observe(initial_timeout):
    magic_value = 8664100
    expected_updates = 5
    payload_pattern = re.compile(r"payload: (\d+)")

    try:
        args = goliothctl_args() + ["logs", "listen", "--json", device_name()]

        logging.info("running goliothctl with args=%s", args)
        goliothctl = pexpect.spawn("goliothctl", args)

        time.sleep(2) # Wait 2s to be (almost) sure that logs will show updates counter
        counter_expected = magic_value
        counter_set(counter_expected - 1)

        # Workaround LightDB issue, which does not consume requests too often
        time.sleep(1)

        counter_set(counter_expected)

        # Allow much more time before first log entry arrives
        timeout = initial_timeout

        while magic_value + expected_updates > counter_expected:
            line = goliothctl_readline(goliothctl, timeout)
            entry = json.loads(line)

            # Once we are connected (log entries arrived), reduce timeout value
            timeout = DEFAULT_TIMEOUT

            assert "message" in entry, f"No 'message' in {entry}"

            payload = payload_pattern.match(entry["message"])
            if not payload:
                continue

            counter_logged = int(payload[1])

            logging.info("Logged %s (expected %s)", counter_logged, counter_expected)

            if counter_logged == counter_expected:
                counter_expected += 1

                # Workaround LightDB issue, which does not consume requests too often
                time.sleep(1)

                counter_set(counter_expected)
    finally:
        goliothctl.terminate()
