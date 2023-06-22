#!/usr/bin/env python3

import json
import os
from pathlib import Path

import asyncclick as click
from golioth import Client
import trio


SCRIPTS_DIR = Path(__file__).parent
RESOURCES_DIR = SCRIPTS_DIR.parent / 'resources'


@click.command()
async def cli():
    client = Client(os.environ.get("GOLIOTHCTL_CONFIG"))
    project = await client.default_project()
    device = await project.device_by_name(os.environ['GOLIOTH_DEVICE_NAME'])

    print('Removing anything under /test')
    await device.lightdb.delete('test')

    for json_file in (RESOURCES_DIR / 'get').glob('*.json'):
        # Workaround for not being able to quickly set different resource paths. Requests always
        # succeed, but unfortunately sometimes data is missing.
        await trio.sleep(0.5)

        lightdb_path = f'test/get/{json_file.stem}'

        with json_file.open('r') as json_fp:
            value = json.load(json_fp)
            print(f'Setting content of {lightdb_path}')
            await device.lightdb.set(lightdb_path, value)

if __name__ == '__main__':
    cli(_anyio_backend='trio')
