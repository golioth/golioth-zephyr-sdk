# Copyright (c) 2022 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

from contextlib import suppress
import json
import logging
import os

import pexpect
import pytest


DEFAULT_TIMEOUT = 30


@pytest.fixture()
def initial_timeout(request):
    timeout = request.config.getoption('--initial-timeout')
    if timeout is None:
        timeout = DEFAULT_TIMEOUT
    return timeout


def goliothctl_readline(goliothctl, timeout):
    index = goliothctl.expect([goliothctl.crlf, goliothctl.delimiter],
                              timeout=timeout)
    if index == 0:
        return goliothctl.before + goliothctl.crlf
    else:
        return goliothctl.before


def test_stream_events_received(initial_timeout):
    try:
        args = []

        with suppress(KeyError):
            args += pexpect.split_command_line(os.environ["GOLIOTHCTL_OPTS"])

        args += ["stream", "listen"]

        with suppress(KeyError):
            args.append(os.environ["GOLIOTH_DEVICE_NAME"])

        logging.info("running goliothctl with args=%s", args)
        goliothctl = pexpect.spawn("goliothctl", args)

        # Allow much more time before first log entry arrives
        timeout = initial_timeout

        for i in range(0, 5):
            line = goliothctl_readline(goliothctl, timeout)
            entry = json.loads(line)

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
    finally:
        goliothctl.terminate()
