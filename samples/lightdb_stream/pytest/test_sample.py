# Copyright (c) 2022 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

from contextlib import suppress
import json
import logging
import os
import pexpect
import pytest


def test_stream_events_received():
    try:
        args = []

        with suppress(KeyError):
            args += pexpect.split_command_line(os.environ["GOLIOTHCTL_OPTS"])

        args += ["stream", "listen"]

        with suppress(KeyError):
            args.append(os.environ["GOLIOTH_DEVICE_NAME"])

        print(f"running goliothctl with {args=}")
        goliothctl = pexpect.spawn("goliothctl", args)

        for i in range(0, 10):
            line = goliothctl.readline()
            entry = json.loads(line)

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


if __name__ == "__main__":
    pytest.main()
