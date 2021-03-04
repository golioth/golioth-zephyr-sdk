import argparse
import os
from pathlib import Path
import subprocess

from west import log
from west.commands import WestCommand, CommandError
from west.manifest import MANIFEST_PROJECT_INDEX
from west.app.project import _ProjectCommand

class Patch(_ProjectCommand):
    def __init__(self):
        super().__init__(
            'patch',
            'patch west subprojects',
            'Runs "git am patches/<project>/" on each of the specified projects.')

    def do_add_parser(self, parser_adder):
        parser = self._parser(parser_adder)
        parser.add_argument('-a', '--apply', action='store_true',
                            help='use "git apply" instead of "git am"')
        parser.add_argument('projects', metavar='PROJECT', nargs='*',
                            help='''projects (by name or path) to operate on;
                            defaults to active cloned projects''')
        return parser

    def do_run(self, args, ignored):
        manifest_project_path = Path(self.manifest.projects[MANIFEST_PROJECT_INDEX].abspath)
        am = 'apply' if args.apply else 'am --keep-cr'
        failed = []
        for project in self._cloned_projects(args):
            patches_path = manifest_project_path / 'patches' / project.name
            if not patches_path.exists():
                continue

            log.banner(f'patching {project.name_and_path}):')
            patches = filter(Path.is_file, patches_path.glob('*.patch'))

            for patch in sorted(patches):
                try:
                    log.inf(f'git {am} {patch.name}')
                    project.git(f'{am} {patch}')
                except subprocess.CalledProcessError:
                    failed.append(project)
                    continue
        self._handle_failed(args, failed)
