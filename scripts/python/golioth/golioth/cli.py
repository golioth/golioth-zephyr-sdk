#!/usr/bin/env python3

from base64 import b64decode
from datetime import datetime, timezone
import json
from pathlib import Path
from typing import Optional, Tuple, Union

import asyncclick as click
from colorama import init, Fore, Style
from rich.console import Console

from golioth import Client, LogEntry, LogLevel, RPCTimeout

console = Console()

class Config:
    def __init__(self):
        self.config_path: Optional[Path] = None

pass_config = click.make_pass_decorator(Config, ensure=True)

@click.group()
@click.option('-c', '--config-path', type=Path,
              help='Path to goliothctl configuration')
@pass_config
def cli(config, config_path):
    config.config_path = config_path


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
            client = Client(config.config_path)
            project = await client.default_project()
            # console.log(f'client: {client}')
            device = await project.device_by_name(device_name)
            # console.log(f'device: {device}')

            resp = await device.rpc.call(method, params)
    except RPCTimeout as e:
        console.print(f'Timeout on RPC method: {method}')
        return

    console.print(resp)


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


def log_format_zephyr(log: LogEntry) -> str:
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


def log_format_default(log: LogEntry) -> str:
    return f'[{log.datetime}] <{log.level.name}> {log.module} {log.message}'


def print_log_default(log: LogEntry):
    console.print(log_format_default(log))


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
    client = Client(config.config_path)
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
            client = Client(config.config_path)
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
    with console.status(f'Getting settings...'):
        client = Client(config.config_path)
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
            client = Client(config.config_path)
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
            client = Client(config.config_path)
            project = await client.default_project()

            await project.settings.delete(key)
    except KeyError:
        console.print(f'No such setting with key {key}')
        return


def main():
    cli(_anyio_backend='trio')


if __name__ == '__main__':
    main()
