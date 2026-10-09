#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Validate the native shader container and its OpenGNM register/linkage ABI."""
import argparse, pathlib, struct


def native_crc(data):
    # PSBC's shader checksum uses the reflected CRC polynomial with
    # its native index reduction, rather than the standard zlib checksum.
    crc = 0xffffffff
    for value in data:
        index = (value ^ crc) & 255
        index = index - 6 * ((((index - index // 7) >> 1) + index // 7) >> 2) if index < 224 else 0
        item = index
        for _ in range(8):
            item = (item >> 1) ^ (0xedb88320 if item & 1 else 0)
        crc = item ^ (crc >> 8)
    return (~crc) & 0xffffffff


def native_identity(data):
    result = 0xcbf29ce484222325
    for value in data:
        result = ((result ^ value) * 0x100000001b3) & 0xffffffffffffffff
    return result


def validate(data):
    def require(condition, message):
        if not condition:
            raise ValueError(message)
    companion = None
    if len(data) >= 16 and data[-16:-12] == b'KGFX':
        _, version, companion_offset, companion_size = struct.unpack_from('<4sIII', data, len(data) - 16)
        require(version == 1 and companion_offset >= 112 and companion_size >= 112,
                'Invalid native precision companion footer')
        require(companion_offset + companion_size == len(data) - 16,
                'Precision companion extent outside package')
        companion = validate(data[companion_offset:companion_offset + companion_size])
        require(companion['stage'] == 2, 'Only fragment stages have a precision companion')
        data = data[:companion_offset]
    require(len(data) >= 112 and data[36:40] == b'Shdr', 'Missing native shader file header')
    stage = data[44]
    require(stage in (1, 2) and data[47] == 1, 'Expected base-PS4 vertex or fragment shader')
    common = struct.unpack_from('<I', data, 52)[0]
    count, shader_size = common >> 24, common & 0x7fffff
    code_start = 52 + data[45] * 4
    orb = code_start + shader_size - 28
    require(code_start >= 92 and orb + 28 <= len(data), 'Shader extent outside container')
    require(data[orb:orb + 7] == b'OrbShdr', 'Native binary info is missing at the declared extent')
    code_size = struct.unpack_from('<I', data, orb + 8)[0] >> 8
    require(code_size > 0 and code_size % 4 == 0, 'Invalid ISA size')
    require(data[orb + 13] == count, 'File and runtime input-slot counts disagree')
    slots_start = 52 + (40 if stage == 1 else 60)
    trailer_start = orb - data[orb + 12] * 4 - count * 4
    require(trailer_start == code_start + code_size, 'Input usage table must follow the ISA without an overflowing back-pointer')
    slots = data[slots_start:slots_start + count * 4]
    require(slots == data[trailer_start:trailer_start + count * 4], 'File and runtime usage tables disagree')
    expected_crc = struct.unpack_from('<I', data, orb + 24)[0]
    require(native_crc(data[code_start:orb + 24]) == expected_crc, 'Native shader CRC mismatch')
    usage = [tuple(slots[i:i + 4]) for i in range(0, len(slots), 4)]
    pairs = set()
    for kind, api, register, flags in usage:
        require(kind in (0x12, 0x17, 0x1c), 'Unexpected shader resource usage')
        require(register % 2 == 0 and register < 15, 'Native pointer must use an aligned user-SGPR pair')
        require(register not in pairs, 'Overlapping native pointer arguments')
        pairs.add(register)
    inputs, exports, interpolants = [], [], []
    words = struct.unpack_from('<' + 'I' * (code_size // 4), data, code_start)
    if stage == 1:
        input_count, export_count = data[88], data[89]
        semantic_start = slots_start + count * 4
        inputs = [tuple(data[semantic_start + i * 4:semantic_start + i * 4 + 4]) for i in range(input_count)]
        export_start = semantic_start + input_count * 4
        exports = [tuple(data[export_start + i * 2:export_start + i * 2 + 2]) for i in range(export_count)]
        if inputs:
            fetch = [register for kind, api, register, flags in usage if kind == 0x12]
            require(len(fetch) == 1 and any(item[0] == 0x17 for item in usage), 'Vertex inputs require fetch and buffer-table pointers')
            require(words[0] == 0xbe802100 | fetch[0], 'Fetch call must use the declared pointer and return through s0:s1')
            require(all(size == 4 and vgpr >= 4 for _, vgpr, size, _ in inputs), 'Invalid dynamically fetched vertex register layout')
        parameters = {(word >> 4) & 63 for word in words if word & 0xfc000000 == 0xf8000000}
        require(all(32 + (packed & 31) in parameters for _, packed in exports), 'Metadata declares a varying without a hardware PARAM export')
    else:
        input_count = data[108]
        semantic_start = slots_start + count * 4
        inputs = [tuple(data[semantic_start + i * 2:semantic_start + i * 2 + 2]) for i in range(input_count)]
        require(len({semantic for semantic, _ in inputs}) == input_count, 'Duplicate pixel input semantic')
        # Base-PS4 VINTRP encodes its dense input index in bits 10..15.
        # Inspect only the ISA extent, never the trailing metadata or companion.
        interpolants = sorted({(word >> 10) & 63 for word in words if word & 0xfc000000 == 0xc8000000})
        require(set(range(input_count)).issubset(interpolants),
                'Pixel input metadata lacks distinct hardware interpolation locations')
    identity = struct.unpack_from('<Q', data, orb + 16)[0]
    require(identity == native_identity(data[36:orb]),
            'Native cache identity does not cover emitted ISA and register/interface metadata')
    return {'fp32_companion': companion, 'stage': stage, 'code_bytes': code_size, 'slots': usage, 'inputs': inputs, 'exports': exports, 'interpolants': interpolants, 'cache_identity': identity}


def main():
    p = argparse.ArgumentParser()
    p.add_argument('files', type=pathlib.Path, nargs='+')
    args = p.parse_args()
    for path in args.files:
        info = validate(path.read_bytes())
        print(f'{path.name}: native ABI valid, {info["code_bytes"]} ISA bytes')


if __name__ == '__main__':
    main()
