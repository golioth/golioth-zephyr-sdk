# Copyright (c) 2022 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

from contextlib import suppress
import json
import logging
import os
from pathlib import Path
import re
import subprocess
import time

import pexpect
import pytest


DEFAULT_TIMEOUT = 30
PROJECT_NAME = "dfu"
NEW_VERSION = "2.0.0"


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


def create_dummy_firmware_sysbuild(running_dir) -> str:
    new_fw_path = os.path.join(running_dir, "new-firmware.bin")

    logging.info("Extracting signature file")
    cmake = subprocess.run(["cmake", "-LA", "-N", running_dir], stdout=subprocess.PIPE,
                           check=True)
    key_file_match = re.search(rb'mcuboot_CONFIG_BOOT_SIGNATURE_KEY_FILE[^=]*="(.*)"', cmake.stdout)
    if not key_file_match:
        raise RuntimeError("Key file could not be extracted")

    key_file = key_file_match[1].decode()

    logging.info("Creating dummy firmware %s", new_fw_path)
    cmd = ["west", "sign",
           "-d", os.path.join(running_dir, PROJECT_NAME),
           "-t", "imgtool",
           "--no-hex",
           "-B", new_fw_path,
           "--",
           "--key", key_file,
           "--version", NEW_VERSION]

    logging.info("Signing dummy firmware: %s", cmd)
    subprocess.run(cmd, check=True)

    return new_fw_path


def create_dummy_firmware_ncs(running_dir) -> str:
    logging.info("Replacing mcuboot version")

    config_path = Path(running_dir) / "zephyr" / ".config"
    config_old = config_path.read_text()
    config_new = re.sub(f'CONFIG_MCUBOOT_IMAGE_VERSION=.*',
                        f'CONFIG_MCUBOOT_IMAGE_VERSION="{NEW_VERSION}"',
                        config_old)
    config_path.write_text(config_new)

    cmd = ["west", "build", "-d", running_dir]
    logging.info("Running %s", cmd)
    subprocess.run(cmd, check=True)

    return os.path.join(running_dir, "zephyr", "app_update.bin")


def create_dummy_firmware(running_dir) -> str:
    if (Path(running_dir) / "pm.config").is_file():
        return create_dummy_firmware_ncs(running_dir)

    if (Path(running_dir) / PROJECT_NAME).is_dir():
        return create_dummy_firmware_sysbuild(running_dir)

    raise RuntimeError("Unsupported build directory structure")


def release_rollout(firmware_path: str):
    logging.info("Creating artifact")
    subprocess.run(["goliothctl"] + goliothctl_args() +
                   ["dfu", "artifact", "create", firmware_path,
                    "--version", NEW_VERSION],
                   check=True)
    logging.info("Creating release (with rollout)")
    subprocess.run(["goliothctl"] + goliothctl_args() +
                   ["dfu", "release", "create",
                    "--rollout", "true",
                    "--release-tags", NEW_VERSION,
                    "--components", f"main@{NEW_VERSION}"],
                   check=True)


def release_delete():
    logging.info("Removing release")
    subprocess.run(["goliothctl"] + goliothctl_args() +
                   ["dfu", "release", "delete",
                    "--release-tags", NEW_VERSION],
                   check=True)
    logging.info("Removing artifact")
    subprocess.run(["goliothctl"] + goliothctl_args() +
                   ["dfu", "artifact", "delete", NEW_VERSION],
                   check=True)


def expect_diagnostics_entry(goliothctl, timeout=None):
    if timeout is None:
        timeout = goliothctl.timeout

    end_time = time.time() + timeout
    time_left = timeout

    while time_left > 0:
        index = goliothctl.expect([goliothctl.crlf, goliothctl.delimiter],
                                  timeout=time_left)
        if index == 0:
            line = goliothctl.before + goliothctl.crlf
        else:
            line = goliothctl.before

        entry = json.loads(line)

        if "type" not in entry or entry["type"] != "DIAGNOSTICS":
            time_left = end_time - time.time()
            continue

        assert "metadata" in entry, "No metadata in diagnostics log entry"
        assert "state" in entry["metadata"], "No state in diagnostics log entry"
        assert "target" in entry["metadata"], "No target in diagnostics log entry"
        assert "version" in entry["metadata"], "No version in diagnostics log entry"

        return entry


def test_dfu(cmdopt, initial_timeout):
    try:
        args = goliothctl_args() + ["logs", "listen", "--json", device_name()]

        logging.info("running goliothctl with args=%s", args)
        goliothctl = pexpect.spawn("goliothctl", args)

        time.sleep(2) # Wait 2s to be (almost) sure that logs will show updated firmware version

        firmware_path = create_dummy_firmware(cmdopt)
        try:
            release_rollout(firmware_path)

            downloading_target = None

            for i in range(0, 2):
                entry = expect_diagnostics_entry(goliothctl, initial_timeout)

                logging.info("Diagnostics: %s", entry)

                if entry["metadata"]["state"] == "DOWNLOADING":
                    downloading_target = entry["metadata"]["target"]
                    break

            assert downloading_target is not None, "Downloading has not started yet"
            assert downloading_target == NEW_VERSION, "Incorrect target version"

            entry = expect_diagnostics_entry(goliothctl, 5 * 60)
            logging.info("Diagnostics: %s", entry)
            assert entry["metadata"]["state"] == "DOWNLOADED", "Incorrect state"
            assert entry["metadata"]["target"] == NEW_VERSION, "Incorrect target version"

            entry = expect_diagnostics_entry(goliothctl, 10)
            logging.info("Diagnostics: %s", entry)
            assert entry["metadata"]["state"] == "UPDATING", "Incorrect state"
            assert entry["metadata"]["target"] == NEW_VERSION, "Incorrect target version"

            entry = expect_diagnostics_entry(goliothctl, 2 * 60 + initial_timeout)
            logging.info("Diagnostics: %s", entry)
            assert entry["metadata"]["state"] == "IDLE", "Incorrect state"
            assert entry["metadata"]["version"] == NEW_VERSION, "Incorrect version"
        finally:
            release_delete()
    finally:
        goliothctl.terminate()
