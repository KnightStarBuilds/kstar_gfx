#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""One validated scene catalog shared by builds, launchers and packaging."""
import argparse
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / 'assets/scenes.json'


def load(path=CATALOG):
    data = json.loads(Path(path).read_text())
    if data.get('schema') != 1 or not data.get('scenes'):
        raise ValueError('Invalid scene catalog')
    seen = set()
    for scene in data['scenes']:
        name = scene['id']
        if not re.fullmatch('[a-z][a-z0-9_]*', name) or name in seen:
            raise ValueError('Invalid or duplicate scene ID: ' + name)
        seen.add(name)
        for key in ('views', 'sequences', 'resources'):
            if len(set(scene[key])) != len(scene[key]):
                raise ValueError('Duplicate ' + key + ' in ' + name)
        if 'overview' not in scene['views'] or 'idle' not in scene['sequences']:
            raise ValueError('Scenes require overview and idle defaults')
    if data['default'] not in seen:
        raise ValueError('Unknown default scene')
    return data


def generate(data):
    q = json.dumps
    lines = ['// Generated from assets/scenes.json. SPDX-License-Identifier: MIT',
             '#pragma once', '#include <array>', '#include <span>', '#include <string_view>',
             'namespace kgfx_demo_catalog {',
             'struct Scene { std::string_view id, title; bool walkthrough;',
             '  std::span<const std::string_view> views, sequences, resources; };',
             'inline constexpr std::string_view default_scene = ' + q(data['default']) + ';']
    for scene in data['scenes']:
        for key in ('views', 'sequences', 'resources'):
            values = ', '.join(q(value) for value in scene[key])
            lines.append('inline constexpr std::array<std::string_view, %d> %s_%s = {%s};' %
                         (len(scene[key]), scene['id'], key, values))
    lines.append('inline constexpr std::array<Scene, %d> scenes = {{' % len(data['scenes']))
    for scene in data['scenes']:
        name = scene['id']
        lines.append('  {%s, %s, %s, %s_views, %s_sequences, %s_resources},' %
                     (q(name), q(scene['title']), str(scene['walkthrough']).lower(), name, name, name))
    lines += ['}};', 'constexpr const Scene* find(std::string_view id) {',
              '  for (const auto& scene : scenes) if (scene.id == id) return &scene;',
              '  return nullptr;', '}',
              'constexpr bool contains(std::span<const std::string_view> values, std::string_view value) {',
              '  for (auto item : values) if (item == value) return true;',
              '  return false;', '}', '} // namespace kgfx_demo_catalog', '']
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--catalog', type=Path, default=CATALOG)
    parser.add_argument('--header', type=Path)
    parser.add_argument('--list', action='store_true')
    parser.add_argument('--list-legacy', action='store_true')
    parser.add_argument('--list-showcase', action='store_true')
    args = parser.parse_args()
    data = load(args.catalog)
    if args.header:
        text = generate(data)
        if not args.header.exists() or args.header.read_text() != text:
            args.header.parent.mkdir(parents=True, exist_ok=True)
            args.header.write_text(text)
    if args.list or args.list_legacy or args.list_showcase:
        print(';'.join(scene['id'] for scene in data['scenes'] if args.list or scene['walkthrough']==args.list_showcase))


if __name__ == '__main__':
    main()
