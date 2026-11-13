#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Catalog source of truth and transitive shader dependency validation."""
import json
from pathlib import Path
import tempfile
import unittest
from scene_catalog import load, generate, ROOT
from shader_source import expand


class Catalog(unittest.TestCase):
    def test_showcase_contract_and_resource_recipes(self):
        catalog=load();scenes={scene['id']:scene for scene in catalog['scenes']}
        self.assertEqual(catalog['default'],'facility')
        self.assertEqual(len(scenes),12)
        self.assertEqual(set(scenes)-{'triangle','cubes','instances','offscreen','stress','pbr'},
                         {'facility','glass_lab','waterworks','boiler_room','lighting_baked','lighting_dynamic'})
        manifest=json.loads((ROOT/'assets/manifest.json').read_text())
        sources={entry['file'] for entry in manifest['files']}
        for recipe in manifest['cook']:
            self.assertIn(recipe['source'],sources)
        header=generate(catalog)
        for scene in scenes.values():
            self.assertIn('"'+scene['id']+'"',header)
            if scene['walkthrough']:
                self.assertEqual(scene['sequences'],['idle','interact','tour'])
                self.assertTrue(scene['resources'])

    def test_reject_duplicate_scene_identifier(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'.tmp') as directory:
            path=Path(directory)/'scenes.json';data=load();data['scenes'].append(data['scenes'][0])
            path.write_text(json.dumps(data))
            with self.assertRaises(ValueError):load(path)

    def test_shader_includes_are_recursive_and_content_sensitive(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'.tmp') as directory:
            directory=Path(directory);source=directory/'test.frag';a=directory/'a.glsl';b=directory/'b.glsl'
            source.write_text('#version 450\n#include "a.glsl"\nvoid main() {}\n')
            a.write_text('#include "b.glsl"\n');b.write_text('const float value=1;\n')
            dependencies=set();first=expand(source,dependencies=dependencies)
            self.assertEqual(dependencies,{source,a,b});self.assertIn('value=1',first)
            b.write_text('const float value=2;\n');self.assertNotEqual(first,expand(source))
            b.write_text('#include "a.glsl"\n')
            with self.assertRaisesRegex(ValueError,'Cyclic'):expand(source)
            a.write_text('#include "../outside.glsl"\n')
            with self.assertRaisesRegex(ValueError,'within'):expand(source)


if __name__=='__main__':
    (ROOT/'.tmp').mkdir(exist_ok=True)
    unittest.main()
