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


def test_lightdb_counter_received(initial_timeout):
    expected_updates = 5

    try:
        args = []

        with suppress(KeyError):
            args += pexpect.split_command_line(os.environ["GOLIOTHCTL_OPTS"])

        args += ["lightdb", "listen"]

        with suppress(KeyError):
            args.append(os.environ["GOLIOTH_DEVICE_NAME"])

        args += ["/counter"]

        logging.info("running goliothctl with args=%s", args)
        goliothctl = pexpect.spawn("goliothctl", args)

        # Allow much more time before first log entry arrives
        timeout = initial_timeout

        line = goliothctl_readline(goliothctl, timeout)
        counter = json.loads(line)
        logging.info("Ignoring initial value %s", counter)

        counters = set()

        for i in range(0, expected_updates + 1):
            line = goliothctl_readline(goliothctl, timeout)
            counter = json.loads(line)

            # Once we are connected (log entries arrived), reduce timeout value
            timeout = DEFAULT_TIMEOUT

            counters.add(counter)

            logging.info("Counter updated to %s", counter)
    finally:
        goliothctl.terminate()

    for i in range(1, expected_updates + 1):
        assert i in counters, f"No counter value {i} in collected counters"
