#!/usr/bin/env python3

from dataclasses import dataclass
import os
from pathlib import Path
from random import randint
import sys
import trio


# ZEPHYR_BASE = GOLIOTH_BASE.parents[2] / 'zephyr'
# from runners.core import BuildConfiguration


TRAILER_MAGIC = bytes([
    0x77, 0xc2, 0x95, 0xf3,
    0x60, 0xd2, 0xef, 0x7f,
    0x35, 0x52, 0x50, 0x0f,
    0x2c, 0xb6, 0x79, 0x80,
])

#
# Steps:
# - when there is no flash.bin, then create one from zephyr.signed.exe
#   - create file and truncate it according to total flash size (from DT)
#   - copy zephyr.signed.exe to slot0 partiiton
#   - write trailer magic bytes (16)
#   - write 'swap_info' (or whatever it is that "confirms" an image); alternatively just sign image
#     with imgtool with --confirm option, so it is marked right away
# - always inspect flash.bin and check slot1 'swap_info' field
# - swap slot0 with slot1 according to 'swap_info'
#   - maybe we need to change 'test' to 'revert' when swapping
# - (optional, in future) respect 'revert/test' image types of slot0
# - copy slot0 from flash.bin to slot0.exe and execute it as child process
# - whatever exit code there is, just do everything in a loop according to test/revert swap_info
#   and/or 'image ok' information
#

@dataclass
class Slot:
    offset: int
    content: bytes = b''

SLOT_SIZE = 0x800000
slots = [
    Slot(0x800000),
    Slot(0x1000000),
]


def create_initial_flash_bin(zephyr_exe: Path, flash_bin: Path):
    print(f'Creating empty {flash_bin}')
    with flash_bin.open('wb') as flash_fp:
        flash_fp.truncate(4 * SLOT_SIZE)

        flash_fp.seek(slots[0].offset)

        print('Filling up slot0')
        with (zephyr_exe.parent / 'zephyr.signed.exe').open('rb') as zephyr_fp:
            flash_fp.write(zephyr_fp.read())


async def main():
    zephyr_exe = Path(sys.argv[1]).resolve()
    flash_bin = zephyr_exe.parent / 'flash.bin'
    zephyr_args = sys.argv[2:]

    if not flash_bin.exists():
        create_initial_flash_bin(zephyr_exe, flash_bin)

    while True:
        cmd = [str(zephyr_exe)] + zephyr_args + [f'-seed={randint(0, 2 ** 32 - 1)}'] + [f'-flash={str(flash_bin)}']
        print(f'Executing {cmd}')
        await trio.run_process(cmd)

        print(f'Looking at {flash_bin} contents')

        with flash_bin.open('r+b') as flash_fp:
            # Read slots contents
            for slot in slots:
                flash_fp.seek(slot.offset)
                slot.content = flash_fp.read(SLOT_SIZE)

            # Check magic
            if slots[1].content.endswith(TRAILER_MAGIC):
                print('Trailer magic is correct')

            # Check "Swap info"
            flash_fp.seek(slots[1].offset + SLOT_SIZE - 16 - 16 - 24)
            print('Checking whole trailer')
            print(flash_fp.read(24).hex())
            print(slots[1].content[-16 - 16 - 8:-16 - 16].hex())

            # flash_fp.seek(slots[1].offset + SLOT_SIZE - len(TRAILER_MAGIC) - 16 - 8)
            # swap_info = ord(flash_fp.read(1))
            swap_info = slots[1].content[-len(TRAILER_MAGIC) - 16 - 8:][0]

            print(f'Swap info: {swap_info}')

            # Swap slot0 with slot1
            slots[0].content, slots[1].content = slots[1].content, slots[0].content

            # Write swapped contents
            for slot in slots:
                flash_fp.seek(slot.offset)
                flash_fp.write(slot.content)

        slot1_exe = zephyr_exe.parent / 'slot1.exe'
        with open(slot1_exe, 'wb') as slot1_fp:
            slot1_fp.write(slots[1].content[0x200:])

        os.chmod(slot1_exe, 0o755)

        await trio.sleep(1)


if __name__ == '__main__':
    trio.run(main)
