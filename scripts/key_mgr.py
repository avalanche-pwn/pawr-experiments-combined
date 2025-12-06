# Copyright (c) 2019 Foundries.io
# Copyright (c) 2022 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

'''example_west_command.py

Example of a west extension in the example-application repository.'''

from west.commands import WestCommand  # your extension must subclass this
from west import log                   # use this for user output
from west.util import west_topdir
import subprocess
import secrets
import os
import json

KEYS_PATH = os.path.join(west_topdir(), "keys.json")
KEY_LEN = 32
class KeyMgr(WestCommand):
    def __init__(self):
        super().__init__(
            'keymgr',               # gets stored as self.name
            'Manager for advertiser and scanner keys',  # self.help
            # self.description:
            '''\
Allows management of keys and device id's
''')

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(self.name,
                                         help=self.help,
                                         description=self.description)

        parser.add_argument("--generate", help="Generate new key", action="store_true")
        parser.add_argument("--dev-id", help="Id of the device to use", type=str)
        parser.add_argument("--manufacturer-id", help="Manufacturer dev id", type=str)
        parser.add_argument("--export-key", help="Display devices key", action="store_true")

        parser.add_argument("--cmake-dump", help="Generate header file with keys", type=str)

        return parser

    def _get_key_obj(self):
        if os.path.exists(KEYS_PATH):
            with open(KEYS_PATH, "r") as f:
                keys_obj = json.loads(f.read())
            return {key: keys_obj[key] for key in sorted(keys_obj)}
        return {}

    def _save_key_obj(self, key_obj):
        with open(KEYS_PATH, "w") as f:
            f.write(json.dumps(key_obj, indent=2))

    def _generate_key(self, args):
        if not args.dev_id or not args.manufacturer_id:
            print("You need to specify both --dev-id and --manufacturer-id")

        keys_obj = self._get_key_obj()

        result = {
            "manufacturer_id": args.manufacturer_id,
            "key": secrets.token_hex(KEY_LEN)
        }
        keys_obj[args.dev_id] = result

        self._save_key_obj(keys_obj)

    def _cmake_dump(self, args):
        keys_obj = self._get_key_obj()
        with open(args.cmake_dump, "w") as f:
            f.write(f"uint8_t keys[{len(keys_obj)}][{KEY_LEN}] = {{\n")
            for dev_id, dev_obj in keys_obj.items():
                values = [f"0x{elem:x}" for elem in bytearray.fromhex(dev_obj["key"])]
                f.write(f'    {{{", ".join(values)}}},\n')
            f.write("};\n")

    def _fetch_manufacturer_id(self, args):
        print(self._get_key_obj()[args.dev_id]["manufacturer_id"], end="")

    def _export_key(self, args):
        if not args.dev_id:
            print("You need to specify device id")
            return

        keys_obj = self._get_key_obj()
        print(keys_obj[args.dev_id]["key"])

    def do_run(self, args, unknown_args):
        if args.generate:
            self._generate_key(args)
            return
        if args.export_key:
            self._export_key(args)
            return
        if args.cmake_dump:
            self._cmake_dump(args)
            return
        self._fetch_manufacturer_id(args)

