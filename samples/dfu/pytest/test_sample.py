# Copyright (c) 2022 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

from contextlib import asynccontextmanager
from functools import cached_property
import logging
from operator import attrgetter
import os
from pathlib import Path
import re
import subprocess

from golioth import Artifact, Client, LogEntry, LogsMonitor, Project, Release
from imgtool.image import Image, VerifyResult
import pytest
import trio


# This is the same as using the @pytest.mark.anyio on all test functions in the module
pytestmark = pytest.mark.anyio


PROJECT_NAME = Path(__file__).parents[1].name


@pytest.fixture(scope='session')
async def project():
    client = Client(os.environ.get("GOLIOTHCTL_CONFIG"))
    return await client.default_project()


@pytest.fixture(scope='session')
async def device(project):
    return await project.device_by_name(os.environ['GOLIOTH_DEVICE_NAME'])


@pytest.fixture(scope='function')
async def logs_monitor(device):
    async with device.logs_monitor() as logs_monitor:
        yield logs_monitor


class DiagnosticsCollector:
    def __init__(self, logs_monitor: LogsMonitor):
        self.logs_monitor: LogsMonitor = logs_monitor
        self.logs: list[LogEntry] = []

    def get_all(self) -> list[LogEntry]:
        return self.logs

    async def expect(self, timeout: int, state: str) -> LogEntry:
        with trio.fail_after(timeout):
            while True:
                log = await self.logs_monitor.get()

                if log.type != 'DIAGNOSTICS':
                    continue

                logging.info("Diagnostics: %s", log)
                self.logs.append(log)

                if log.metadata['state'] == state:
                    return log


@pytest.fixture(scope='function')
async def diagnostics(logs_monitor):
    return DiagnosticsCollector(logs_monitor)


class Firmware:
    def __init__(self, path: Path):
        self.path = path

    @cached_property
    def version(self) -> str:
        verify_result, version_bin, _ = Image.verify(str(self.path), None)
        if verify_result != VerifyResult.OK:
            raise RuntimeError('Invalid firmware file')

        return '.'.join([str(x) for x in version_bin[:3]])


def create_dummy_firmware_sysbuild(running_dir: Path, version: str) -> Path:
    new_fw_path = running_dir / "new-firmware.bin"

    logging.info("Extracting signature file")
    cmake = subprocess.run(["cmake", "-LA", "-N", str(running_dir)],
                           stdout=subprocess.PIPE,
                           check=True)
    key_file_match = re.search(rb'mcuboot_(?:espressif_)?' + \
                               rb'CONFIG_BOOT_SIGNATURE_KEY_FILE[^=]*="(.*)"',
                               cmake.stdout)
    if not key_file_match:
        raise RuntimeError("Key file could not be extracted")

    key_file = key_file_match[1].decode()

    logging.info("Creating dummy firmware %s", str(new_fw_path))
    cmd = ["west", "sign",
           "-d", str(running_dir / PROJECT_NAME),
           "-t", "imgtool",
           "--no-hex",
           "-B", str(new_fw_path),
           "--",
           "--version", version]

    if key_file:
        cmd += ["--key", key_file]

    logging.info("Signing dummy firmware: %s", cmd)
    subprocess.run(cmd, check=True)

    return new_fw_path


def create_dummy_firmware_ncs(running_dir: Path, version: str) -> Path:
    logging.info("Replacing mcuboot version")

    config_path = running_dir / "zephyr" / ".config"
    config_old = config_path.read_text()
    config_new = re.sub(f'CONFIG_MCUBOOT_IMAGE_VERSION=.*',
                        f'CONFIG_MCUBOOT_IMAGE_VERSION="{version}"',
                        config_old)
    config_path.write_text(config_new)

    cmd = ["west", "build", "-d", str(running_dir)]
    logging.info("Running %s", cmd)
    subprocess.run(cmd, check=True)

    return running_dir / "zephyr" / "app_update.bin"


def create_dummy_firmware(running_dir: Path, version: str) -> Path:
    if (running_dir / "pm.config").is_file():
        return create_dummy_firmware_ncs(running_dir, version)

    if (running_dir / PROJECT_NAME).is_dir():
        return create_dummy_firmware_sysbuild(running_dir, version)

    raise RuntimeError("Unsupported build directory structure")


@asynccontextmanager
async def temp_artifact(project: Project, firmware: Firmware):
    logging.info("Creating artifact")
    artifact = await project.artifacts.upload(firmware.path,
                                              version=firmware.version)
    try:
        yield artifact
    finally:
        logging.info("Removing artifact")
        await project.artifacts.delete(artifact.id)


@asynccontextmanager
async def temp_release(project: Project, artifact: Artifact):
    logging.info("Creating release (with rollout)")
    release = await project.releases.create(artifact_ids=[artifact.id],
                                            tags=[artifact.version],
                                            rollout=True)
    try:
        yield release
    finally:
        logging.info("Removing release")
        await project.releases.delete(release.id)


@asynccontextmanager
async def temp_release_with_artifact(project: Project, firmware: Firmware):
    async with temp_artifact(project, firmware) as artifact:
        async with temp_release(project, artifact) as release:
            yield release


async def test_dfu(cmdopt, initial_timeout, project, diagnostics):
    # Wait 2s to be (almost) sure that logs will show updated firmware version
    # TODO: register on logs 2s from the past, so that sleep won't be needed
    await trio.sleep(2)

    firmware = Firmware(create_dummy_firmware(Path(cmdopt), '2.0.0'))
    async with temp_release_with_artifact(project, firmware):
        log = await diagnostics.expect(initial_timeout, 'DOWNLOADING')
        assert log.metadata['target'] == firmware.version, 'Incorrect target version'

        log = await diagnostics.expect(5 * 60, 'DOWNLOADED')
        assert log.metadata['target'] == firmware.version, 'Incorrect target version'

        #
        # FIXME: Since server-side can reorder diagnostics notifications, do not
        # expect 'UPDATING' to be after 'DOWNLOADING', as reordering happens
        # very often. Instead, check if device has sent those two diagnostics
        # messages in correct order by sorting all received logs by timestamp.
        #
        # log = await diagnostics.expect(10, 'UPDATING')
        # assert log.metadata['target'] == NEW_VERSION, 'Incorrect target version'

        log = await diagnostics.expect(2 * 60 + initial_timeout, 'IDLE')
        assert log.metadata['version'] == firmware.version, 'Incorrect version'

    # Check order of reported diagnostics
    expected_states = ['IDLE', 'DOWNLOADING', 'DOWNLOADED', 'UPDATING', 'IDLE']
    sorted_logs = sorted(diagnostics.get_all(), key=attrgetter('datetime'))
    sorted_states = [log.metadata['state'] for log in
                     sorted_logs[-len(expected_states):]]
    logging.info('Sorted states: %s', sorted_states)
    assert expected_states == sorted_states, 'Invalid diagnostics order'
