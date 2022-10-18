# Copyright (c) 2022 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

import base64
from contextlib import suppress
import json
import logging
import os
import re
import struct

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


def goliothctl_readline(goliothctl, timeout):
    index = goliothctl.expect([goliothctl.crlf, goliothctl.delimiter],
                              timeout=timeout)
    if index == 0:
        return goliothctl.before + goliothctl.crlf
    else:
        return goliothctl.before


def get_match_counter(entry, match):
    return int(match[1])


def get_hexdump_counter(entry, match):
    hexdump = entry["metadata"]["hexdump"]
    assert isinstance(hexdump, str), "hexdump is not a string"
    binary_value = base64.decodebytes(hexdump.encode())
    return struct.unpack("<L", binary_value)[0]


def check_log_contents(root_entry, entry, counter: int, fields):
    for field_name, data in fields.items():
        if isinstance(data, dict):
            # Recursively check contents of dict
            check_log_contents(root_entry, entry[field_name], counter, data)
        else:
            assert data == entry[field_name]


def test_lightdb_counter_observe(initial_timeout):
    counter_min_expected = 1
    counter_max_expected = 5
    counter_max = -1
    payload_pattern = re.compile(r"payload: (\d+)")
    expected_logs = [
        (r"Debug info! (\d+)", get_match_counter, {"level": "DEBUG", "metadata": {"func": "main"}}),
        (r"Log 1: (\d+)", get_match_counter, {"level": "DEBUG", "metadata": {"func": "func_1"}}),
        (r"Log 2: (\d+)", get_match_counter, {"level": "DEBUG", "metadata": {"func": "func_2"}}),
        (r"Warn: (\d+)", get_match_counter, {"level": "WARN"}),
        (r"Err: (\d+)", get_match_counter, {"level": "ERROR"}),
        (r"Counter hexdump", get_hexdump_counter, {"level": "INFO"}),
    ]

    try:
        args = goliothctl_args() + ["logs", "listen", "--json", device_name()]

        logging.info("running goliothctl with args=%s", args)
        goliothctl = pexpect.spawn("goliothctl", args)

        # Allow much more time before first log entry arrives
        timeout = initial_timeout

        log_batches = {}

        while counter_max <= counter_max_expected + 1:
            line = goliothctl_readline(goliothctl, timeout)
            entry = json.loads(line)

            # Once we are connected (log entries arrived), reduce timeout value
            timeout = DEFAULT_TIMEOUT

            assert "message" in entry, f"No 'message' in {entry}"
            assert "metadata" in entry, f"No 'metadata' in {entry}"
            assert "index" in entry["metadata"], f"No 'index' in metadata of {entry}"

            pattern_found = False

            for pattern_index, (pattern, get_counter, fields) in enumerate(expected_logs):
                match = re.match(pattern, entry["message"])
                if not match:
                    continue

                counter = get_counter(entry, match)
                counter_max = max(counter_max, counter)

                logging.info("Got log with counter %s and pattern index %s (%s)",
                             counter, pattern_index, pattern)

                if counter not in log_batches:
                    log_batches[counter] = set()
                log_batches[counter].add(pattern_index)

                check_log_contents(entry, entry, counter, fields)
                pattern_found = True
                break

            if not pattern_found:
                logging.warning("Ignoring log entry: %s", entry)

        # Check if all expected logs arrived
        for counter in range(counter_min_expected, counter_max_expected + 1):
            assert counter in log_batches, f"No log entries with {counter=}"
            pattern_indexes = log_batches[counter]
            assert len(pattern_indexes) == len(expected_logs), \
                f"Did not receive all log entries for {counter=}: {pattern_indexes=}"
    finally:
        goliothctl.terminate()
