#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Expand local shader includes identically for GL and GNM offline compilers."""
import argparse
from pathlib import Path
import re

INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"\n]+)"\s*(?://.*)?$')


def expand(source, stack=(), dependencies=None):
    source = Path(source).resolve()
    if source in stack:
        raise ValueError('Cyclic shader include: ' + ' -> '.join(p.name for p in (*stack, source)))
    if dependencies is not None:
        dependencies.add(source)
    result = []
    for line in source.read_text().splitlines(keepends=True):
        match = INCLUDE.match(line.rstrip('\n'))
        if match:
            relative = Path(match[1])
            if relative.is_absolute() or '..' in relative.parts:
                raise ValueError('Shader includes must stay within their source directory')
            result += ['// begin include ' + relative.as_posix() + '\n',
                       expand(source.parent / relative, (*stack, source), dependencies),
                       '// end include ' + relative.as_posix() + '\n']
        else:
            result.append(line)
    return ''.join(result)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    args.destination.write_text(expand(args.source))


if __name__ == '__main__':
    main()
