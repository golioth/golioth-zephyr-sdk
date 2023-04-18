#!/usr/bin/env python3

from base64 import b64decode
from datetime import datetime, timezone
import fnmatch
import json
from pathlib import Path
import re
import sys
from typing import Optional, Tuple, Union

import asyncclick as click
from colorama import init, Fore, Style
from imgtool.image import Image, VerifyResult
from rich.console import Console

from golioth import Client, LogEntry, LogLevel, RPCTimeout

create_set = set

console = Console()

class Config:
    def __init__(self):
        self.config_path: Optional[Path] = None
        self.api_key: Optional[str] = None

pass_config = click.make_pass_decorator(Config, ensure=True)

@click.group()
@click.option('-c', '--config-path', type=Path,
              help='Path to goliothctl configuration')
@click.option('--api-key', help='Api key')
@pass_config
def cli(config, config_path, api_key):
    config.config_path = config_path
    config.api_key = api_key


def rpc_params(params: str) -> Union[list, dict]:
    parsed = json.loads(params)

    return parsed

@cli.command()
@click.option('-d', '--device-name',
              help='Name of device on which RPC method will be called',
              required=True)
@click.argument('method')
@click.argument('params', required=False, type=rpc_params, nargs=-1)
@pass_config
async def call(config, device_name, method, params):
    """Call RPC method on device."""

    if len(params) == 1 and isinstance(params[0], list):
        params = params[0]

    try:
        with console.status(f'Waiting for reply from method {method}...'):
            client = Client(config.config_path, api_key=config.api_key)
            project = await client.default_project()
            # console.log(f'client: {client}')
            device = await project.device_by_name(device_name)
            # console.log(f'device: {device}')

            resp = await device.rpc.call(method, params)
    except RPCTimeout as e:
        console.print(f'Timeout on RPC method: {method}')
        return

    console.print(resp)


class MatchGlobRegexSwitch(click.ParamType):
    @staticmethod
    def regex(pattern: str):
        return pattern

    @staticmethod
    def glob(pattern: str):
        return fnmatch.translate(pattern)

    def convert(self, value, param, ctx):
        return getattr(self, value)


MATCH_GLOB_REGEX_SWITCH = MatchGlobRegexSwitch()


@cli.group()
def artifacts():
    """DFU artifacts related commands."""
    pass


@artifacts.command()
@click.option('-p', '--package', default='main', show_default=True,
              help='Package name')
@click.option('-r', 'release_rollout',
              help='Create release with the same tag (when specified once) and rollout (when specified twice).',
              count=True)
@click.option('-f', '--force',
              help='Force upload, by removing conflicting artifacts (and/or releases).',
              is_flag=True)
@click.argument('path', type=click.Path(exists=True))
@pass_config
async def upload(config, package, release_rollout, force, path):
    """Upload new DFU artifact."""
    create_release = (release_rollout > 0)

    verify_result, version_bin, digest = Image.verify(path, None)
    if verify_result != VerifyResult.OK:
        raise RuntimeError('Invalid firmware file')

    version = '.'.join([str(x) for x in version_bin[:3]])

    with console.status('Uploading DFU artifact...'):
        client = Client(config.config_path, api_key=config.api_key)
        project = await client.default_project()

        if force:
            artifacts_to_remove = create_set()
            artifacts = await project.artifacts.get_all()

            artifacts_to_remove.update([a for a in artifacts
                                        if (a.version == version and
                                            a.package == package)])

            releases_to_remove = create_set()
            releases = await project.releases.get_all()

            if create_release:
                releases_to_remove.update([r for r in releases
                                           if version in r.tags])

            for artifact in artifacts_to_remove:
                releases_to_remove.update([r for r in releases
                                           if artifact.id in r.artifact_ids])

            for release in releases_to_remove:
                await project.releases.delete(release.id)

            for artifact in artifacts_to_remove:
                await project.artifacts.delete(artifact.id)

        artifact = await project.artifacts.upload(Path(path), version, package)
        console.print(artifact)

        if create_release:
            release = await project.releases.create(artifact_ids=[artifact.id],
                                                    tags=[version],
                                                    rollout=(release_rollout > 1))

            console.print(release)


@artifacts.command()
@pass_config
async def list(config):
    """List DFU artifacts."""
    with console.status('Getting DFU artifacts...'):
        client = Client(config.config_path, api_key=config.api_key)
        project = await client.default_project()

        artifacts = await project.artifacts.get_all()
        for artifact in artifacts:
            console.print(artifact)


@artifacts.command()
@click.argument('artifact_id', nargs=-1, required=True)
@click.option('--by-package-version', '--pv', is_flag=True,
              help='Select artifact by "package@version" instead of artifact ID.')
@click.option('--hidden-glob', 'match_type', flag_value='glob',
              default=True,
              hidden=True,
              type=MATCH_GLOB_REGEX_SWITCH,
              help='Use glob match.')
@click.option('--regex', '-r', 'match_type', flag_value='regex',
              type=MATCH_GLOB_REGEX_SWITCH,
              help='Use regex match (instead of glob).')
@pass_config
async def delete(config, artifact_id, by_package_version, match_type):
    """Delete DFU artifact.

    \b
    Example invocations
    -------------------

    \b
    Delete artifact with package 'main' and version '1.0.0':
    $ golioth artifacts delete --pv main@1.0.0

    \b
    Delete all artifacts:
    $ golioth artifacts delete '*'

    \b
    Delete all version 2.x.x and 3.x.x artifacts from package 'main':
    $ golioth artifacts delete --pv 'main@[2-3].*.*'
    """
    if by_package_version:
        def artifact_match(pattern, artifact):
            return pattern.match(f'{artifact.package}@{artifact.version}') is not None
    else:
        def artifact_match(pattern, artifact):
            return pattern.match(artifact.id) is not None

    with console.status('Getting DFU artifacts...'):
        client = Client(config.config_path, api_key=config.api_key)
        project = await client.default_project()

        patterns = [re.compile(match_type(x)) for x in artifact_id]
        deleted = []

        for artifact in await project.artifacts.get_all():
            for pattern in patterns:
                if artifact_match(pattern, artifact):
                    await project.artifacts.delete(artifact.id)
                    console.print(f'Deleted: {artifact}')
                    deleted.append(artifact)
                    break

        if not deleted:
            console.print('No artifacts deleted!')


@cli.group()
def releases():
    """DFU releases related commands."""
    pass


@releases.command()
@click.option('artifact_ids', '--artifact', '-a', multiple=True, required=True)
@click.option('tags', '--tag', '-t', multiple=True, required=True)
@click.option('--rollout', '-r', is_flag=True)
@pass_config
async def create(config, artifact_ids, tags, rollout):
    """Create DFU release."""
    with console.status('Creating DFU release...'):
        client = Client(config.config_path, api_key=config.api_key)
        project = await client.default_project()

        await project.releases.create(artifact_ids, tags, rollout)


@releases.command()
@click.argument('release_id', nargs=-1, required=True)
@click.option('--by-tag', '-t', is_flag=True,
              help='Select release by release tag instead of release ID.')
@click.option('--hidden-glob', 'match_type', flag_value='glob',
              default=True,
              hidden=True,
              type=MATCH_GLOB_REGEX_SWITCH,
              help='Use glob match.')
@click.option('--regex', '-r', 'match_type', flag_value='regex',
              type=MATCH_GLOB_REGEX_SWITCH,
              help='Use regex match (instead of glob).')
@pass_config
async def delete(config, release_id, by_tag, match_type):
    """Delete DFU releases.

    \b
    Example invocations
    -------------------

    \b
    Delete release with tag '1.0.0':
    $ golioth releases delete -t 1.0.0

    \b
    Delete all releases:
    $ golioth releases delete '*'

    \b
    Delete all version 2.x.x and 3.x.x releases:
    $ golioth releases delete -t '[2-3].*.*'
    """
    if by_tag:
        def artifact_match(pattern, release):
            for tag in release.tags:
                if pattern.match(tag):
                    return True

            return False
    else:
        def artifact_match(pattern, release):
            return pattern.match(release.id) is not None

    with console.status('Deleting DFU releases...'):
        client = Client(config.config_path, api_key=config.api_key)
        project = await client.default_project()

        patterns = [re.compile(match_type(r)) for r in release_id]
        deleted = []

        for release in await project.releases.get_all():
            for pattern in patterns:
                if artifact_match(pattern, release):
                    await project.releases.delete(release.id)
                    console.print(f'Deleted: {release}')
                    deleted.append(release)
                    break

        if not deleted:
            console.print('No releases deleted!')


@releases.command()
@pass_config
async def list(config):
    """List DFU releases."""
    with console.status('Getting DFU releases...'):
        client = Client(config.config_path, api_key=config.api_key)
        project = await client.default_project()

        releases = await project.releases.get_all()
        for release in releases:
            console.print(release)


@cli.group()
def lightdb():
    """LightDB State related commands."""
    pass

@lightdb.command()
@click.option('-d', '--device-name',
              help='Name of device',
              required=True)
@click.argument('path')
@pass_config
async def get(config, device_name, path):
    """Get LightDB State value."""
    path = path.strip('/')

    with console.status('Getting LightDB State value...'):
        client = Client(config.config_path, api_key=config.api_key)
        project = await client.default_project()
        device = await project.device_by_name(device_name)

        resp = await device.lightdb.get(path)

        console.print(resp)


@lightdb.command()
@click.option('-d', '--device-name',
              help='Name of device',
              required=True)
@click.argument('path')
@click.argument('value', type=json.loads)
@pass_config
async def set(config, device_name, path, value):
    """Set LightDB State value."""
    path = path.strip('/')

    with console.status('Setting LightDB State value...'):
        client = Client(config.config_path, api_key=config.api_key)
        project = await client.default_project()
        device = await project.device_by_name(device_name)

        await device.lightdb.set(path, value)


@lightdb.command()
@click.option('-d', '--device-name',
              help='Name of device',
              required=True)
@click.argument('path')
@pass_config
async def delete(config, device_name, path):
    """Delete LightDB State value."""
    path = path.strip('/')

    with console.status('Deleting LightDB State value...'):
        client = Client(config.config_path, api_key=config.api_key)
        project = await client.default_project()
        device = await project.device_by_name(device_name)

        await device.lightdb.delete(path)


@lightdb.command()
@click.option('-d', '--device-name',
              help='Name of device',
              required=True)
@click.argument('path')
@pass_config
async def monitor(config, device_name, path):
    """Monitor LightDB State path."""
    path = path.strip('/')

    with console.status('Monitoring LightDB State path...'):
        client = Client(config.config_path, api_key=config.api_key)
        project = await client.default_project()
        device = await project.device_by_name(device_name)

        async for value in device.lightdb.iter(path):
            console.print(value)


@cli.group()
def certificate():
    """Certificates related commands."""
    pass


@certificate.command()
@pass_config
async def list(config):
    """Get certificates."""
    with console.status('Getting certificates...'):
        client = Client(config.config_path, api_key=config.api_key)
        project = await client.default_project()

        certs = await project.certificates.get_all()

        console.print([c.info for c in certs])


@certificate.command()
@click.argument('id')
@pass_config
async def info(config, id):
    """Get certificate by ID."""
    with console.status('Getting certificate...'):
        client = Client(config.config_path, api_key=config.api_key)
        project = await client.default_project()

        resp = await project.certificates.get(id)

        console.print(resp.info)


@certificate.command()
@click.option('-t', '--cert_type', type=click.Choice(['root', 'intermediate']), default='root')
@click.argument('cert_file', type=Path)
@pass_config
async def add(config, cert_type, cert_file):
    """Add certificate."""
    with console.status('Adding certificate...'):
        client = Client(config.config_path, api_key=config.api_key)
        project = await client.default_project()

        with cert_file.open('rb') as fp:
            resp = await project.certificates.add(cert_pem=fp.read(), cert_type=cert_type)

        console.print(resp)


@certificate.command()
@click.argument('id')
@pass_config
async def delete(config, id):
    """Delete certificate by ID."""
    with console.status(f'Deleting certificate {id}...'):
        client = Client(config.config_path, api_key=config.api_key)
        project = await client.default_project()

        resp = await project.certificates.delete(cert_id=id)

        console.print(resp)


@cli.group()
def device():
    """Device related commands."""
    pass


@device.command()
@click.argument('name')
@pass_config
async def info(config, name):
    """Get info about device."""
    with console.status(f'Getting device {name} info...'):
        client = Client(config.config_path, api_key=config.api_key)
        project = await client.default_project()

        device = await project.device_by_name(name)

        console.print(device.info)


@device.command()
@pass_config
async def list(config):
    """List all devices."""
    with console.status('Getting devices...'):
        client = Client(config.config_path, api_key=config.api_key)
        project = await client.default_project()

        devices = await project.get_devices()

        console.print([d.info for d in devices])


@cli.group()
def logs():
    """Logging service related commands."""
    pass


def level_map(level: LogLevel) -> Tuple[str, str, str]:
    if level == LogLevel.ERR:
        return 'err', Fore.RED + Style.BRIGHT, Style.RESET_ALL
    elif level == LogLevel.WRN:
        return 'wrn', Fore.YELLOW + Style.BRIGHT, Style.RESET_ALL
    elif level == LogLevel.DBG:
        return 'dbg', Style.DIM, Style.RESET_ALL

    return level.name.lower(), '', ''


def format_hexdump(indent_size: int, data: bytes):
    indent = ' ' * indent_size

    return indent + ' '.join([f'{x:02x}' for x in data])


def log_format_default(log: LogEntry) -> str:
    return f'[{log.datetime}] <{log.level.name}> {log.module} {log.message}'


def print_log_default(log: LogEntry):
    console.print(log_format_default(log))


def log_format_zephyr(log: LogEntry) -> str:
    if 'uptime' not in log.metadata:
        return f'GENERIC {log_format_default(log)}'

    ts = datetime.fromtimestamp(log.metadata['uptime'] / 1000000, tz=timezone.utc)
    ts_str = ts.strftime('%H:%M:%S') + f'.{ts.microsecond // 1000:03},{ts.microsecond % 1000:03}'

    level, pre, post = level_map(log.level)

    pre_msg = f'[{ts_str}] {pre}<{level}> {log.module}: '

    if 'func' in log.metadata:
        pre_msg += f'{log.metadata["func"]}: '

    formatted = f'{pre_msg}{log.message}'

    if 'hexdump' in log.metadata:
        formatted += '\n'
        formatted += format_hexdump(len(pre_msg) - len(pre),
                                    b64decode(log.metadata['hexdump']))

    if post:
        formatted += post

    return formatted


def print_log_zephyr(log: LogEntry):
    print(log_format_zephyr(log))


@logs.command()
@click.option('-d', '--device-name',
              help='Name of device from which logs should be printed')
@click.option('-f', '--follow',
              help='Continuously print new entries as they are appended to the logging service',
              is_flag=True)
@click.option('-n', '--lines',
              help='Limit the number of log entries shows',
              type=int, default=20)
@click.option('--format',
              help='Format',
              type=click.Choice(['default', 'zephyr']))
@pass_config
async def tail(config, device_name, follow, lines, format):
    """Show the most recent log entries."""
    client = Client(config.config_path, api_key=config.api_key)
    project = await client.default_project()

    if device_name:
        device = await project.device_by_name(device_name)
        logs_provider = device
    else:
        logs_provider = project

    if format == 'zephyr':
        print_log = print_log_zephyr
    else:
        print_log = print_log_default

    if follow:
        async for log in logs_provider.logs_iter(lines=lines):
            print_log(log)

    logs = await logs_provider.get_logs()
    for log in logs[-lines:]:
        print_log(log)


@cli.group()
def settings():
    """Settings service related commands."""
    pass


@settings.command()
@click.argument('key')
@pass_config
async def get(config, key):
    """Get setting value of KEY."""
    try:
        with console.status(f'Getting setting value of {key}...'):
            client = Client(config.config_path, api_key=config.api_key)
            project = await client.default_project()

            resp = await project.settings.get(key)
    except KeyError:
        console.print(f'No such setting with key {key}')
        return

    console.print(resp)


@settings.command()
@pass_config
async def get_all(config):
    """Get all settings values."""
    with console.status('Getting settings...'):
        client = Client(config.config_path, api_key=config.api_key)
        project = await client.default_project()

        resp = await project.settings.get_all()

        console.print(resp)


@settings.command()
@click.argument('key')
@click.argument('value', type=json.loads)
@pass_config
async def set(config, key, value):
    """Set setting value of KEY to VALUE."""
    try:
        with console.status(f'Setting {key} to {value}...'):
            client = Client(config.config_path, api_key=config.api_key)
            project = await client.default_project()

            resp = await project.settings.set(key, value)
    except KeyError:
        console.print(f'No such setting with key {key}')
        return

    console.print(resp)


@settings.command()
@click.argument('key')
@pass_config
async def delete(config, key):
    """Delete KEY from settings."""
    try:
        with console.status(f'Deleting {key} from settings...'):
            client = Client(config.config_path, api_key=config.api_key)
            project = await client.default_project()

            await project.settings.delete(key)
    except KeyError:
        console.print(f'No such setting with key {key}')
        return


def main():
    cli(_anyio_backend='trio')


if __name__ == '__main__':
    main()
