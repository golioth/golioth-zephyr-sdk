# Copyright (c) 2022 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

import base64
from contextlib import suppress
import json
import logging
import os
from pathlib import Path
import re
import struct
import sys
from typing import Any, Callable, Dict, List, Match, Tuple, Optional

from golioth import Client, LogEntry
import pytest
import trio


# This is the same as using the @pytest.mark.anyio on all test functions in the module
pytestmark = pytest.mark.anyio


DEFAULT_TIMEOUT = 30


@pytest.fixture()
def initial_timeout(request):
    timeout = request.config.getoption('--initial-timeout')
    if timeout is None:
        timeout = DEFAULT_TIMEOUT
    return timeout


@pytest.fixture(scope='session')
async def device():
    client = Client(os.environ.get("GOLIOTHCTL_CONFIG"))
    project = await client.default_project()
    return await project.device_by_name(os.environ['GOLIOTH_DEVICE_NAME'])


def get_match_counter(entry, match) -> int:
    return int(match[1])


def get_hexdump_counter(entry, match) -> int:
    hexdump = entry["metadata"]["hexdump"]
    assert isinstance(hexdump, str), "hexdump is not a string"
    binary_value = base64.decodebytes(hexdump.encode())
    return struct.unpack("<L", binary_value)[0]


def check_log_contents(entry: dict, counter: int, fields):
    for field_name, data in fields.items():
        if isinstance(data, dict):
            # Recursively check contents of dict
            check_log_contents(entry[field_name], counter, data)
        else:
            assert data == entry[field_name]


def process_log(expected_logs: List[Tuple[str,
                                          Callable[[dict, Match], Optional[int]],
                                          Dict[str, Any]]],
                log_batches: Dict[int, Any],
                log: LogEntry) -> Optional[int]:
    assert log.message, f"No 'message' in {log}"
    assert log.metadata, f"No 'metadata' in {log}"
    assert log.metadata['index'], f"No 'index' in metadata of {log}"

    counter = None

    for pattern_index, (pattern, get_counter, fields) in enumerate(expected_logs):
        match = re.match(pattern, log.message)
        if not match:
            continue

        counter = get_counter(log.info, match)

        logging.info("Got log with counter %s and pattern index %s (%s)",
                     counter, pattern_index, pattern)

        if counter not in log_batches:
            log_batches[counter] = set()

        log_batches[counter].add(pattern_index)

        check_log_contents(log.info, counter, fields)
        break

    if counter is None:
        logging.warning("Ignoring log entry: %s", log)

    return counter


async def test_lightdb_counter_observe(initial_timeout, device):
    counter_min_expected = 1
    counter_max_expected = 5
    counter_max = -1
    expected_logs = [
        (r"Debug info! (\d+)", get_match_counter, {"level": "DEBUG", "metadata": {"func": "main"}}),
        (r"Log 1: (\d+)", get_match_counter, {"level": "DEBUG", "metadata": {"func": "func_1"}}),
        (r"Log 2: (\d+)", get_match_counter, {"level": "DEBUG", "metadata": {"func": "func_2"}}),
        (r"Warn: (\d+)", get_match_counter, {"level": "WARN"}),
        (r"Err: (\d+)", get_match_counter, {"level": "ERROR"}),
        (r"Counter hexdump", get_hexdump_counter, {"level": "INFO"}),
    ]

    # Allow much more time before first log entry arrives
    timeout = initial_timeout

    log_batches = {}

    async with device.logs_monitor() as logs_monitor:
        while counter_max <= counter_max_expected + 1:
            with trio.fail_after(timeout):
                log = await logs_monitor.get()

            # Once we are connected (log entries arrived), reduce timeout value
            timeout = DEFAULT_TIMEOUT

            counter = process_log(expected_logs, log_batches, log)

            if counter is not None:
                counter_max = max(counter_max, counter)


    # Check if all expected logs arrived
    for counter in range(counter_min_expected, counter_max_expected + 1):
        assert counter in log_batches, f"No log entries with {counter=}"
        pattern_indexes = log_batches[counter]
        assert len(pattern_indexes) == len(expected_logs), \
            f"Did not receive all log entries for {counter=}: {pattern_indexes=}"
