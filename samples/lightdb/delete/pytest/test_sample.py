# Copyright (c) 2022 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

from contextlib import suppress
import logging
import os
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


def test_lightdb_counter_delete(initial_timeout):
    magic_value = 15664155

    try:
        args = goliothctl_args() + ["lightdb", "listen", device_name(), "/counter"]

        logging.info("running goliothctl with args=%s", args)
        goliothctl = pexpect.spawn("goliothctl", args)

        # Allow much more time before first log entry arrives
        timeout = initial_timeout

        line = ''

        for _ in range(0, 5):
            line = goliothctl_readline(goliothctl, timeout).strip().decode()

            # Once we are connected (log entries arrived), reduce timeout value
            timeout = DEFAULT_TIMEOUT

            logging.info("Updated counter value: %s (expected %s)", line, magic_value)

            if line == str(magic_value):
                break

            # Workaround LightDB issue, which does not consume requests too often
            time.sleep(1)

            counter_set(magic_value)

        assert line == str(magic_value), "Did not receive magic value"

        for _ in range(0, 3):
            line = goliothctl_readline(goliothctl, timeout).strip().decode()

            # Once we are connected (log entries arrived), reduce timeout value
            timeout = DEFAULT_TIMEOUT

            logging.info("Updated counter value: %s (expected %s)", line, 'null')

            if line == 'null':
                logging.info("Received 'null'")
                break

        assert line == 'null', "Did not receive 'null' value as expected"
    finally:
        goliothctl.terminate()
