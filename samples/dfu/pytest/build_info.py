from dataclasses import dataclass
from pathlib import Path
import re
from typing import Dict, Union

PROJECT_NAME = Path(__file__).parents[1].name

class BuildConfiguration:
    '''This helper class provides access to build-time configuration.

    Configuration options can be read as if the object were a dict,
    either object['CONFIG_FOO'] or object.get('CONFIG_FOO').

    Kconfig configuration values are available (parsed from .config and
    .config.sysbuild).'''

    def __init__(self, build_dir: str):
        self.build_dir = build_dir
        self.options: Dict[str, Union[str, int]] = {}
        self.paths = [Path(self.build_dir) / 'zephyr' / c
                      for c in ['.config', '.config.sysbuild']]
        self._parse()

    def __contains__(self, item):
        return item in self.options

    def __getitem__(self, item):
        return self.options[item]

    def get(self, option, *args):
        return self.options.get(option, *args)

    def getboolean(self, option):
        '''If a boolean option is explicitly set to y or n,
        returns its value. Otherwise, falls back to False.
        '''
        return self.options.get(option, False)

    def _parse(self):
        for path in self.paths:
            if path.exists():
                self._parse_file(path)

    def _parse_file(self, path):
        opt_value = re.compile('^(?P<option>CONFIG_[A-Za-z0-9_]+)=(?P<value>.*)$')
        not_set = re.compile('^# (?P<option>CONFIG_[A-Za-z0-9_]+) is not set$')

        with path.open('r') as f:
            for line in f:
                match = opt_value.match(line)
                if match:
                    value = match.group('value').rstrip()
                    if value.startswith('"') and value.endswith('"'):
                        # A string literal should have the quotes stripped,
                        # but otherwise be left as is.
                        value = value[1:-1]
                    elif value == 'y':
                        # The character 'y' is a boolean option
                        # that is set to True.
                        value = True
                    else:
                        # Neither a string nor 'y', so try to parse it
                        # as an integer.
                        try:
                            base = 16 if value.startswith('0x') else 10
                            self.options[match.group('option')] = int(value, base=base)
                            continue
                        except ValueError:
                            pass

                    self.options[match.group('option')] = value
                    continue

                match = not_set.match(line)
                if match:
                    # '# CONFIG_FOO is not set' means a boolean option is false.
                    self.options[match.group('option')] = False

@dataclass
class BuildInfo:
    conf: BuildConfiguration
    path: Path
    variant: str

    def __init__(self, build_dir: Path):
        if (build_dir / "pm.config").is_file():
            self.variant = 'ncs'
            self.path = build_dir
            self.conf = BuildConfiguration(str(self.path))
        elif (build_dir / PROJECT_NAME).is_dir():
            self.variant = 'sysbuild'
            self.path = build_dir / PROJECT_NAME
            self.conf = BuildConfiguration(str(self.path))
        else:
            raise RuntimeError("Unsupported build directory structure")
