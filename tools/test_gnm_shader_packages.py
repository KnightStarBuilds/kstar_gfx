#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check compiled shader ABI, precision variants, provenance and corruption rejection."""
import argparse, hashlib, json, pathlib, struct
from validate_gnm_shader import validate


def main():
    p = argparse.ArgumentParser()
    p.add_argument('directory', type=pathlib.Path)
    args = p.parse_args()
    files = sorted(args.directory.glob('*.sb'))
    assert files, 'No compiled native shader packages'
    for path in files:
        data = path.read_bytes()
        info = validate(data)
        if info['stage'] == 2:
            assert info['fp32_companion'], path.name + ' lacks its 32F export variant'
            _, _, offset, size = struct.unpack_from('<4sIII', data, len(data) - 16)
            # GnmPsStageRegisters contains SPI_SHADER_COL_FORMAT at byte 80.
            # Shadow-only stages may suppress their unused color export.
            fast_format = struct.unpack_from('<I', data, 80)[0] & 15
            precise_format = struct.unpack_from('<I', data, offset + 80)[0] & 15
            assert fast_format in (0, 4) and precise_format in (0, 9), (path.name, fast_format, precise_format)
            if fast_format != precise_format:
                assert info['cache_identity'] != info['fp32_companion']['cache_identity'], path.name + ': export variants alias native shader cache'
            assert not struct.unpack_from('<I', data, 96)[0] & (1 << 24), path.name + ': front-face mode must match Mesa signed-float ABI'
            # Register-only repairs must invalidate the native shader cache,
            # even though the container's ISA checksum remains unchanged.
            changed_register = bytearray(data)
            changed_register[96] ^= 1
            try:
                validate(changed_register)
            except ValueError as error:
                assert 'cache identity' in str(error), error
            else:
                raise AssertionError(path.name + ': stale native register identity accepted')
        metadata_path = path.with_suffix('.native-package.json')
        if metadata_path.exists():
            metadata = json.loads(metadata_path.read_text())
            assert metadata['artifacts'][path.name] == hashlib.sha256(data).hexdigest()
            assert metadata['inputs']['compiler_build']
        reflection_path = path.with_suffix('.json')
        if info['stage'] == 1 and reflection_path.exists():
            reflection = json.loads(reflection_path.read_text())
            assert {item[0] for item in info['inputs']} == {item['location'] for item in reflection.get('inputs', [])}, path.name + ': input location mismatch'
            assert {item[0] - 15 for item in info['exports']} == {item['location'] for item in reflection.get('outputs', [])}, path.name + ': varying linkage mismatch'
        if info['stage'] == 2 and reflection_path.exists():
            reflection = json.loads(reflection_path.read_text())
            assert {item[0] - 15 for item in info['inputs']} == {item['location'] for item in reflection.get('inputs', [])}, path.name + ': fragment location mismatch'
        if info['stage'] == 2 and len(info['inputs']) > 1:
            # Reproduce the former missing RADV location-assignment pass while
            # repairing the CRC, so failure proves ABI validation, not hashing.
            from validate_gnm_shader import native_crc
            aliased = bytearray(data)
            start = 52 + aliased[45] * 4
            end = start + info['code_bytes']
            for offset in range(start, end, 4):
                word = struct.unpack_from('<I', aliased, offset)[0]
                if word & 0xfc000000 == 0xc8000000:
                    struct.pack_into('<I', aliased, offset, word & ~(63 << 10))
            common = struct.unpack_from('<I', aliased, 52)[0]
            orb = start + (common & 0x7fffff) - 28
            struct.pack_into('<I', aliased, orb + 24, native_crc(aliased[start:orb + 24]))
            try:
                validate(aliased)
            except ValueError as error:
                assert 'interpolation locations' in str(error), error
            else:
                raise AssertionError(path.name + ': aliased fragment inputs accepted')
        damaged = bytearray(data)
        code_start = 52 + data[45] * 4
        damaged[code_start:code_start + info['code_bytes']] = bytes(info['code_bytes'])
        try:
            validate(damaged)
        except ValueError:
            pass
        else:
            raise AssertionError(path.name + ': corrupted ISA was accepted')
    print(f'{len(files)} native packages passed: shader CRC, vertex fetch, sparse locations, distinct fragment interpolation, PARAM linkage, FP16/FP32 variants and corrupt-code rejection')


if __name__ == '__main__':
    main()
