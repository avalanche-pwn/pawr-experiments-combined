# Copyright (c) 2019 Foundries.io
# Copyright (c) 2022 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

'''example_west_command.py

Example of a west extension in the example-application repository.'''

from west.commands import WestCommand  # your extension must subclass this
from west import log                   # use this for user output
import subprocess
import os

class ExportCompileCommands(WestCommand):

    def __init__(self):
        super().__init__(
            'export_compile_commands',               # gets stored as self.name
            'an example west extension command',  # self.help
            # self.description:
            '''\
Exposes compile commands file from build dir.
''')

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(self.name,
                                         help=self.help,
                                         description=self.description)

        return parser

    def do_run(self, args, unknown_args):
        dirname = os.path.dirname(__file__)
        log.inf(subprocess.check_output([os.path.join(dirname, "export_compile_commands.sh")]).decode())
