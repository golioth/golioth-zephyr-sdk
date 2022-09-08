#!/usr/bin/env python3
#
# Copyright (c) 2017 Intel Corporation
# Copyright (c) 2022 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0


from argparse import ArgumentParser
import codecs
import io
import json

import cbor2


parser = ArgumentParser(description="Convert resource files to C array contents")
parser.add_argument("-i", "--input", help="Input (source) file in JSON format")
parser.add_argument("-o", "--output", help="Output file")
parser.add_argument("-t", "--content-type", help="Content type of output file",
                    choices=["cbor", "json"])

args = parser.parse_args()


with open(args.input, "r", encoding="utf-8") as fp:
    input_content = json.loads(fp.read())


if args.content_type == "cbor":
    output_content = cbor2.dumps(input_content)
elif args.content_type == "json":
    output_content = bytes(json.dumps(input_content,
                                      separators=(',', ':')),
                           "ascii")


def get_nice_string(list_or_iterator):
    return ", ".join("0x" + str(x) for x in list_or_iterator)


def make_hex(fp, chunk):
    hexdata = codecs.encode(chunk, 'hex').decode("utf-8")
    hexlist = map(''.join, zip(*[iter(hexdata)] * 2))
    s = get_nice_string(hexlist) + ','
    fp.write(s + '\n')


with open(args.output, "w") as fp:
    output_content_fp = io.BytesIO(output_content)
    for chunk in iter(lambda: output_content_fp.read(8), b''):
        make_hex(fp, chunk)
