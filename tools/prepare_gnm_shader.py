#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Specialize common coordinates to native GNM clip depth and render-target UVs."""
import pathlib,sys
source,destination=map(pathlib.Path,sys.argv[1:]);text=source.read_text()
first,newline,rest=text.partition('\n')
if not first.startswith('#version'):raise ValueError('Shader must begin with #version')
text=first+'\n#define KGFX_GNM 1\n'+rest
if source.suffix=='.vert':
    text=first+'\n#define KGFX_GNM 1\n#define main kgfx_user_main\n'+rest+'\n#undef main\nvoid main() { kgfx_user_main(); gl_Position.z = 0.5 * (gl_Position.z + gl_Position.w); }\n'
destination.write_text(text)
