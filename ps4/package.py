#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Package an existing GNM demo build as a locally validated PS4 homebrew PKG.

SFO/GP4 layout follows maxton/LibOrbisPkg (SFO/ParamSfo.cs and GP4/Gp4Project.cs).
The validation and atomic publication pattern follows Nocturne's PS4 packager;
this standalone tool has no Nocturne imports, build steps, or downloads.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import re
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from scene_catalog import load as load_scene_catalog
from shader_source import expand as expand_shader


class PackageError(Exception):
    """An actionable input or package validation failure."""


def regular_file(path, nonempty=True):
    if path.is_symlink() or not path.is_file():
        raise PackageError(f"Required regular file is missing or is a symlink: {path}")
    if nonempty and not path.stat().st_size:
        raise PackageError(f"Required file is empty: {path}")
    with path.open("rb") as stream:
        stream.read(1)


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_json(path):
    regular_file(path)
    return json.loads(path.read_text(encoding="utf-8"))


def relative_path(name):
    if not isinstance(name, str):
        raise PackageError("Package paths must be strings")
    path = PurePosixPath(name)
    if (not name or path.is_absolute() or path.as_posix() != name
            or any(part in {".", ".."} for part in path.parts)
            or any(ord(char) < 32 or char == "\\" for char in name)):
        raise PackageError(f"Invalid relative package path: {name!r}")
    return path


def no_symlinks(path):
    for component in (path, *path.parents):
        if component.is_symlink():
            raise PackageError(f"Package paths must not contain symlinks: {component}")


def inventory(directory):
    no_symlinks(directory)
    if not directory.is_dir():
        raise PackageError(f"Required input directory is missing: {directory}")
    result = []
    for current, directories, files in os.walk(directory, followlinks=False):
        directories.sort()
        for name in directories + sorted(files):
            source = Path(current) / name
            mode = source.lstat().st_mode
            if not (stat.S_ISREG(mode) or stat.S_ISDIR(mode)):
                raise PackageError(f"Package inputs must be regular files/directories: {source}")
            if name == ".DS_Store" or name.startswith("._"):
                continue
            target = source.relative_to(directory).as_posix()
            relative_path(target)
            if stat.S_ISREG(mode) and source.suffix != ".oelf":
                regular_file(source)
                result.append((source, target))
    return sorted(result, key=lambda item: item[1])


def validate_paths(targets):
    seen = {}
    for target in targets:
        path = relative_path(target)
        for entry in [*reversed(path.parents), path]:
            if entry == PurePosixPath("."):
                continue
            name, directory = entry.as_posix(), entry != path
            previous = seen.get(name.casefold())
            if previous and (previous != (name, directory) or not directory):
                raise PackageError(f"Duplicate or case-ambiguous package path: {name}")
            seen[name.casefold()] = (name, directory)


def read_metadata(path):
    metadata = read_json(path)
    keys = {"title", "title_id", "content_id", "app_version", "system_version"}
    if not isinstance(metadata, dict) or set(metadata) != keys:
        raise PackageError("Invalid package metadata fields")
    if not all(isinstance(value, str) for value in metadata.values()):
        raise PackageError("Package metadata values must be strings")
    if not re.fullmatch(r"[A-Z]{4}[0-9]{5}", metadata["title_id"]):
        raise PackageError("title_id must contain four uppercase letters and five digits")
    if not re.fullmatch(r"[A-Z]{2}[0-9]{4}-" + metadata["title_id"] + r"_00-[A-Z0-9]{16}", metadata["content_id"]):
        raise PackageError("content_id must contain the matching title_id and a 16-character suffix")
    if not re.fullmatch(r"[0-9]{2}\.[0-9]{2}", metadata["app_version"]):
        raise PackageError("app_version must use the format 01.00")
    title = metadata["title"]
    if not title.strip() or any(ord(char) < 32 for char in title) or len(title.encode("utf-8")) > 127:
        raise PackageError("title must be nonempty UTF-8 text of at most 127 bytes")
    if not re.fullmatch(r"0x[0-9A-Fa-f]{8}", metadata["system_version"]):
        raise PackageError("system_version must be an eight-digit hexadecimal firmware value")
    return metadata


def validate_elf_header(header, path, types):
    if (len(header) != 64 or header[:7] != b"\x7fELF\x02\x01\x01"
            or struct.unpack_from("<H", header, 18)[0] != 62
            or struct.unpack_from("<H", header, 16)[0] not in types
            or struct.unpack_from("<H", header, 52)[0] != 64):
        raise PackageError(f"Not the expected x86-64 PS4 executable: {path}")


def validate_executables(elf, eboot):
    regular_file(elf)
    regular_file(eboot)
    with elf.open("rb") as stream:
        validate_elf_header(stream.read(64), elf, {2, 3})
    with eboot.open("rb") as stream:
        header = stream.read(32)
        if len(header) != 32 or header[:4] != b"\x4f\x15\x3d\x1d":
            raise PackageError(f"Not a PS4 SELF executable: {eboot}")
        entries = struct.unpack_from("<H", header, 24)[0]
        declared_size = struct.unpack_from("<Q", header, 16)[0]
        elf_offset = 32 + entries * 32
        if not entries or declared_size > eboot.stat().st_size or elf_offset + 64 > declared_size:
            raise PackageError(f"Truncated or invalid PS4 SELF executable: {eboot}")
        stream.seek(elf_offset)
        validate_elf_header(stream.read(64), eboot, {0xFE00, 0xFE10})
    if eboot.stat().st_mtime_ns < elf.stat().st_mtime_ns:
        raise PackageError("eboot.bin predates the ELF; rebuild the selected configuration")


def cache_values(build):
    cache = build / "CMakeCache.txt"
    regular_file(cache)
    result = {}
    for line in cache.read_text(encoding="utf-8").splitlines():
        if line.startswith(("#", "//")) or "=" not in line:
            continue
        key, value = line.split("=", 1)
        if ":" in key:
            result[key.split(":", 1)[0]] = value
    return result


def validate_resources(root, app0):
    resources = app0 / "resources"
    index = read_json(resources / "package-index.json")
    if (not isinstance(index, dict) or index.get("schema") != 1
            or not re.fullmatch(r"[0-9a-f]{64}", str(index.get("cook_identity", "")))
            or not isinstance(index.get("files"), list) or not index["files"]):
        raise PackageError("Invalid resource package index")
    validate_paths(index["files"])
    for name in index["files"]:
        regular_file(resources / relative_path(name))
    stamp = read_json(root / "assets/cooked/cook.stamp")
    outputs = stamp.get("outputs") if isinstance(stamp, dict) else None
    if (not isinstance(stamp, dict) or stamp.get("schema") != 1
            or stamp.get("identity") != index["cook_identity"]
            or not isinstance(outputs, dict) or not outputs):
        raise PackageError("Cooked asset stamp does not match the resource package index")
    validate_paths(outputs)
    outputs=dict(outputs)
    volume_stamp_path=root / "assets/cooked/effects/volumes/cook.stamp"
    if volume_stamp_path.exists():
        volume_stamp=read_json(volume_stamp_path)
        if (volume_stamp.get("schema") != 1 or not isinstance(volume_stamp.get("outputs"), dict)
                or not volume_stamp["outputs"] or volume_stamp.get("identity") != index.get("volume_cook_identity")):
            raise PackageError("Volume cook stamp does not match the resource package index")
        validate_paths(volume_stamp["outputs"])
        outputs.update({"effects/volumes/"+name:digest for name,digest in volume_stamp["outputs"].items()})
    lighting_stamp_path=root / "assets/cooked/lighting/cook.stamp"
    if lighting_stamp_path.exists():
        lighting_stamp=read_json(lighting_stamp_path)
        if (lighting_stamp.get("schema") != 1 or not isinstance(lighting_stamp.get("outputs"), dict)
                or not lighting_stamp["outputs"] or lighting_stamp.get("identity") != index.get("lighting_cook_identity")):
            raise PackageError("Lighting cook stamp does not match the resource package index")
        validate_paths(lighting_stamp["outputs"])
        outputs.update(lighting_stamp["outputs"])
    expected_assets = {"assets/cooked/" + name for name in outputs}
    indexed_assets = {name for name in index["files"] if name.startswith("assets/cooked/")}
    packaged_assets = {"assets/cooked/" + name for _, name in inventory(resources / "assets/cooked")}
    if expected_assets != indexed_assets or expected_assets != packaged_assets:
        raise PackageError("Cooked asset file set differs from the verified cook stamp")
    for name, expected in outputs.items():
        if (not isinstance(expected, str) or not re.fullmatch(r"[0-9a-f]{64}", expected)
                or sha256(resources / "assets/cooked" / name) != expected):
            raise PackageError(f"Cooked asset checksum mismatch: {name}")
    for name in ("bolt_action_rifle_7_62.kgmodel", "fishermans_hat.kgmodel",
                 "qwantani_night.kgenv", "industrial_pipe_and_valve_01.kgenv",
                 "textures/rock_tile_floor_diff_2k.kgtex",
                 "textures/rock_tile_floor_nor_gl_2k.kgtex",
                 "textures/rock_tile_floor_arm_2k.kgtex"):
        target = "assets/cooked/" + name
        regular_file(resources / target)
        if target not in index["files"]:
            raise PackageError(f"Cooked asset is absent from the resource index: {target}")
    catalog_path = root / "assets/scenes.json"
    if sha256(resources / "assets/scenes.json") != sha256(catalog_path):
        raise PackageError("Packaged scene catalog is stale")
    for scene in load_scene_catalog(catalog_path)["scenes"]:
        for asset in scene["resources"]:
            if asset not in outputs:
                raise PackageError("Scene resource is absent from cooked assets: " + asset)
    # Native shaders are appended after the host resource index is generated.
    # Their own manifests bind every bytecode/reflection artifact to a hash.
    shaders = sorted(path.name for path in (root / "shaders").iterdir()
                     if path.suffix in {".vert", ".frag"})
    if not shaders:
        raise PackageError("No demo shader sources found")
    for name in shaders:
        directory = resources / "shaders"
        metadata = read_json(directory / (name + ".native-package.json"))
        artifacts = metadata.get("artifacts") if isinstance(metadata, dict) else None
        if not isinstance(metadata, dict) or metadata.get("schema") != 1 or not isinstance(artifacts, dict) or not artifacts:
            raise PackageError(f"Invalid native shader manifest: {name}")
        if not all(name + suffix in artifacts for suffix in (".sb", ".json")):
            raise PackageError(f"Native shader manifest omits bytecode/reflection: {name}")
        shader_inputs = metadata.get("inputs")
        if not isinstance(shader_inputs, dict) or shader_inputs.get("source") != sha256(root / "shaders" / name):
            raise PackageError(f"Native shader is stale relative to its source: {name}")
        if shader_inputs.get("expanded_source") != hashlib.sha256(expand_shader(root / "shaders" / name).encode()).hexdigest():
            raise PackageError(f"Native shader includes are stale: {name}")
        for artifact, expected in artifacts.items():
            source = directory / relative_path(artifact)
            regular_file(source)
            if not re.fullmatch(r"[0-9a-f]{64}", str(expected)) or sha256(source) != expected:
                raise PackageError(f"Native shader artifact checksum mismatch: {artifact}")
    for name in ("NOTICE.txt", "assets-manifest.json", "toolchain-lock.json",
                 "runtime-lock.json", "kstar_gfx-LICENSE.txt"):
        regular_file(resources / "notices" / name)
    if volume_stamp_path.exists():
        for name in ("volume-manifest.json", "volume-toolchain-lock.json"):
            regular_file(resources / "notices" / name)
    return index


def git_identity(directory):
    def git(*arguments):
        try:
            return subprocess.check_output(["git", "-C", str(directory), *arguments], stderr=subprocess.PIPE)
        except (OSError, subprocess.CalledProcessError) as error:
            raise PackageError(f"Cannot record source identity for {directory}: {error}") from error
    return {"head": git("rev-parse", "HEAD").decode().strip(),
            "status": git("status", "--porcelain", "--untracked-files=normal").decode().splitlines(),
            "tracked_diff_sha256": hashlib.sha256(git("diff", "HEAD", "--binary")).hexdigest()}


def select_tool(sdk, override=None):
    if override:
        tool = Path(override).expanduser().absolute()
    else:
        host = {"Darwin": "macos", "Linux": "linux", "Windows": "windows"}.get(platform.system())
        if host is None:
            raise PackageError(f"Unsupported packaging host: {platform.system()}")
        tool = sdk / "bin" / host / ("PkgTool.Core.exe" if host == "windows" else "PkgTool.Core")
    regular_file(tool)
    if not os.access(tool, os.X_OK):
        raise PackageError(f"PkgTool is not executable: {tool}")
    return tool


def run_tool(tool, *arguments):
    try:
        result = subprocess.run([str(tool), *map(str, arguments)], check=False,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    except OSError as error:
        raise PackageError(f"Cannot execute {tool}: {error}") from error
    # LibOrbisPkg may report validation errors while returning exit code zero.
    if result.returncode or re.search(r"\[ERROR\]|FATAL ERROR|^Error:|^Command line error:|^Usage:", result.stdout, re.MULTILINE | re.IGNORECASE):
        raise PackageError(f"PkgTool {arguments[0]} failed ({result.returncode}):\n{result.stdout.strip()}")
    return result.stdout


def extract_package(tool, candidate, extracted):
    log = run_tool(tool, "pkg_extract", "--verbose", candidate, extracted)
    # PkgTool preserves the PFS user-root directory and omits special metadata
    # entries from it. Extract those by their list index, not their EntryId.
    payload = extracted / "uroot"
    if not (payload / "eboot.bin").is_file():
        entries = sorted(path.relative_to(extracted).as_posix()
                         for path in extracted.rglob("*")) if extracted.exists() else []
        raise PackageError("Package extraction did not produce uroot/eboot.bin:\n"
                           + log + "\nExtracted paths: " + repr(entries[:20]))
    listing = run_tool(tool, "pkg_listentries", candidate)
    log += "\n" + listing
    indices = {}
    for line in listing.splitlines():
        fields = line.split()
        if len(fields) not in {5, 6} or not fields[0].startswith("0x"):
            continue
        if (not re.fullmatch(r"0x[0-9a-fA-F]+", fields[1])
                or not fields[3].isdigit() or fields[-1] in indices):
            raise PackageError("Invalid or duplicate package metadata entry: " + line)
        indices[fields[-1]] = int(fields[3])
    for entry, target in (("PARAM_SFO", "sce_sys/param.sfo"), ("ICON0_PNG", "sce_sys/icon0.png")):
        if entry not in indices:
            raise PackageError(f"Required package metadata entry is absent: {entry}")
        destination = payload / target
        destination.parent.mkdir(parents=True, exist_ok=True)
        temporary = extracted / (entry + ".bin")
        log += "\n" + run_tool(tool, "pkg_extractentry", candidate, indices[entry], temporary)
        regular_file(temporary)
        if destination.exists():
            regular_file(destination)
            if sha256(destination) != sha256(temporary):
                raise PackageError(f"Package metadata and filesystem entries disagree: {target}")
        os.replace(temporary, destination)
    return payload, log


def encode_sfo(metadata):
    strings = {
        "APP_VER": (metadata["app_version"], 8), "CATEGORY": ("gd", 4),
        "CONTENT_ID": (metadata["content_id"], 48), "FORMAT": ("obs", 4),
        "TITLE": (metadata["title"], 128), "TITLE_ID": (metadata["title_id"], 12),
        "VERSION": (metadata["app_version"], 8),
    }
    integers = {"APP_TYPE": 1, "ATTRIBUTE": 2, "DOWNLOAD_DATA_SIZE": 0,
                "PARENTAL_LEVEL": 1, "SYSTEM_VER": int(metadata["system_version"], 16)}
    keys, values, index = bytearray(), bytearray(), bytearray()
    for key in sorted(strings.keys() | integers.keys()):
        if key in strings:
            value, capacity = strings[key]
            value, kind = value.encode("utf-8") + b"\0", 0x0204
        else:
            value, capacity, kind = struct.pack("<I", integers[key]), 4, 0x0404
        index.extend(struct.pack("<HHIII", len(keys), kind, len(value), capacity, len(values)))
        keys.extend(key.encode("ascii") + b"\0")
        values.extend(value.ljust(capacity, b"\0"))
    key_offset = 20 + len(index)
    keys.extend(b"\0" * (-(key_offset + len(keys)) % 4))
    return struct.pack("<4sIIII", b"\0PSF", 0x101, key_offset, key_offset + len(keys),
                       len(strings) + len(integers)) + index + keys + values


def read_sfo(path):
    regular_file(path)
    data = path.read_bytes()
    if len(data) < 20:
        raise PackageError(f"Truncated SFO: {path}")
    magic, version, keys, values, count = struct.unpack_from("<4sIIII", data)
    if (magic != b"\0PSF" or version != 0x101 or not count
            or keys != 20 + count * 16 or not keys < values <= len(data)):
        raise PackageError(f"Invalid SFO header: {path}")
    result, occupied = {}, []
    for index in range(count):
        key_offset, kind, length, capacity, offset = struct.unpack_from("<HHIII", data, 20 + index * 16)
        key_start, start = keys + key_offset, values + offset
        key_end = data.find(b"\0", key_start, values)
        if (not keys <= key_start < values or key_end <= key_start
                or not 0 < length <= capacity or start + capacity > len(data)
                or any(start < end and begin < start + capacity for begin, end in occupied)):
            raise PackageError(f"Invalid SFO entry bounds: {path}")
        name = data[key_start:key_end].decode("ascii")
        value = data[start:start + length]
        if name in result or kind not in {0x0204, 0x0404}:
            raise PackageError(f"Duplicate or unsupported SFO entry: {name}")
        if kind == 0x0404 and (length != 4 or capacity != 4):
            raise PackageError(f"Invalid SFO integer: {name}")
        if kind == 0x0204 and (value[-1:] != b"\0" or b"\0" in value[:-1]):
            raise PackageError(f"Invalid SFO string: {name}")
        if any(data[start + length:start + capacity]):
            raise PackageError(f"Nonzero SFO value padding: {name}")
        result[name] = (kind, capacity, value)
        occupied.append((start, start + capacity))
    return result


def validate_packaged_sfo(original, packaged, package_size, creation_dates=None):
    expected, actual = read_sfo(original), read_sfo(packaged)
    additions = {"PUBTOOLINFO", "PUBTOOLVER"}
    if additions.intersection(expected) or set(actual) != set(expected) | additions:
        raise PackageError("Packaged SFO has unexpected missing or added fields")
    for name, value in expected.items():
        if actual[name] != value:
            raise PackageError(f"Package builder changed original SFO field: {name}")
    # LibOrbisPkg PkgBuilder.BuildPkg adds these two publishing records while
    # retaining the application metadata. Its INFO size is rounded up to MiB.
    if actual["PUBTOOLVER"] != (0x0404, 4, struct.pack("<I", 0x02890000)):
        raise PackageError("Unexpected package publishing tool version in SFO")
    kind, capacity, value = actual["PUBTOOLINFO"]
    if kind != 0x0204 or capacity != 0x200:
        raise PackageError("Unexpected package publishing information type in SFO")
    dates = creation_dates or {datetime.now(timezone.utc).strftime("%Y%m%d")}
    size_mib = (package_size + 0xFFFFF) // (1024 * 1024)
    expected_info = {
        (f"c_date={date},img0_l0_size={size_mib},img0_l1_size=0,"
         "img0_sc_ksize=512,img0_pc_ksize=832\0").encode("ascii") for date in dates}
    if value not in expected_info:
        raise PackageError("Unexpected package publishing date or image sizes in SFO")
    return {"size": packaged.stat().st_size, "sha256": sha256(packaged),
            "original_fields_verified": len(expected),
            "PUBTOOLVER": "0x02890000", "PUBTOOLINFO": value[:-1].decode("ascii")}


def write_gp4(path, stage, metadata, targets):
    project = ET.Element("psproject", fmt="gp4", version="1000")
    volume = ET.SubElement(project, "volume")
    ET.SubElement(volume, "volume_type").text = "pkg_ps4_app"
    ET.SubElement(volume, "volume_id").text = "PS4VOLUME"
    ET.SubElement(volume, "volume_ts").text = "2020-01-01 00:00:00"
    ET.SubElement(volume, "package", content_id=metadata["content_id"], passcode="0" * 32,
                  storage_type="digital50", app_type="full")
    chunks = ET.SubElement(volume, "chunk_info", chunk_count="1", scenario_count="1")
    ET.SubElement(ET.SubElement(chunks, "chunks"), "chunk", id="0", layer_no="0", label="kstar_gfx")
    ET.SubElement(ET.SubElement(chunks, "scenarios", default_id="0"), "scenario",
                  id="0", type="sp", initial_chunk_count="1", label="kstar_gfx").text = "0"
    files = ET.SubElement(project, "files", img_no="0")
    rootdir = ET.SubElement(project, "rootdir")
    directories = {".": rootdir}
    for target in sorted(targets):
        ET.SubElement(files, "file", targ_path=target, orig_path=str(stage / target))
        parent = PurePosixPath(target).parent
        for directory in reversed([parent, *parent.parents]):
            if directory.as_posix() not in directories:
                directories[directory.as_posix()] = ET.SubElement(
                    directories[directory.parent.as_posix()], "dir", targ_name=directory.name)
    ET.indent(project, space="  ")
    ET.ElementTree(project).write(path, encoding="utf-8", xml_declaration=True)


def prepare_inputs(root, build, config):
    no_symlinks(build)
    cached = cache_values(build)
    if (cached.get("KSTAR_GFX_BACKEND") != "GNM"
            or cached.get("CMAKE_GENERATOR") != "Ninja Multi-Config"
            or config not in cached.get("CMAKE_CONFIGURATION_TYPES", "").split(";")
            or Path(cached.get("CMAKE_HOME_DIRECTORY", "")).resolve() != root.resolve()):
        raise PackageError("Selected build directory is not this project's GNM multi-config build")
    metadata = read_metadata(root / "ps4/package.json")
    app0 = build / "app0" / config / "kstar_gfx_demo"
    elf = build / "bin" / config / "kstar_gfx_demo"
    inputs = inventory(app0)
    validate_executables(elf, app0 / "eboot.bin")
    index = validate_resources(root, app0)
    for name in ("libc.prx", "libSceFios2.prx"):
        regular_file(app0 / "sce_module" / name)
    for name in ("kstar_gfx-LICENSE.txt", "OpenGNM-LICENSE.txt", "OpenOrbis-LICENSE.txt",
                 "musl-COPYRIGHT.txt", "sdk-compat.json"):
        regular_file(app0 / "notices" / name)
    provenance = read_json(build / ("provenance-" + config + ".json"))
    if (not isinstance(provenance, dict) or provenance.get("configuration") != config
            or provenance.get("backend") != "GNM"):
        raise PackageError("Build provenance does not match the selected configuration")
    icon = root / "ps4/resources/icon0.png"
    regular_file(icon)
    with icon.open("rb") as stream:
        header = stream.read(33)
    if (len(header) != 33 or header[:8] != b"\x89PNG\r\n\x1a\n"
            or header[8:16] != struct.pack(">I4s", 13, b"IHDR")
            or header[16:24] != struct.pack(">II", 512, 512)
            or header[24:29] != bytes((8, 2, 0, 0, 0))):
        raise PackageError("Package icon must be a noninterlaced 512 x 512 RGB24 PNG without alpha")
    notice = root / "ps4/resources/icon-license.txt"
    regular_file(notice)
    inputs += [(icon, "sce_sys/icon0.png"), (notice, "notices/package-icon-license.txt")]
    validate_paths(["sce_sys/param.sfo", "package-manifest.json", *(target for _, target in inputs)])
    for directory in (build / "stage" / config, build / "pkg" / config):
        no_symlinks(directory)
        for parent in (directory, *directory.parents):
            if parent.exists() and not parent.is_dir():
                raise PackageError(f"Output directory is occupied by a file: {parent}")
        if directory.is_relative_to(app0) or app0.is_relative_to(directory):
            raise PackageError("Output and package inputs must not overlap")
    return metadata, cached, app0, elf, inputs, index, provenance


def package(root, build, config, sdk=None, pkg_tool=None):
    root, build = root.absolute(), build.expanduser().absolute()
    metadata, cached, app0, elf, inputs, index, provenance = prepare_inputs(root, build, config)
    sdk = Path(sdk or os.environ.get("OO_PS4_TOOLCHAIN") or cached.get("OO_PS4_TOOLCHAIN", "")).expanduser().absolute()
    if not (sdk / "lib/crt1.o").is_file():
        raise PackageError("Select the OpenOrbis SDK with --sdk or OO_PS4_TOOLCHAIN")
    tool = select_tool(sdk, pkg_tool or os.environ.get("PS4_PKG_TOOL"))
    version = run_tool(tool, "version").strip()
    if "PkgTool" not in version or "LibOrbisPkg" not in version:
        raise PackageError("Package tool does not identify itself as LibOrbisPkg/PkgTool")
    sources = {name: git_identity(path) for name, path in (
        ("kstar_gfx", root), ("opengnm", root / "deps/opengnm"),
        ("opengnm-psbc", root / "deps/opengnm-psbc"))}
    manifest = {"schema": 1, "configuration": config, "metadata": metadata,
                "source_repositories": sources, "build_provenance": provenance,
                "elf_sha256": sha256(elf), "resource_cook_identity": index["cook_identity"],
                "packager": {"path": str(tool), "sha256": sha256(tool), "version": version},
                "files": {target: {"size": source.stat().st_size, "sha256": sha256(source)}
                          for source, target in inputs}}
    # All preflight above is read-only; publish only after validation and extraction.
    stage_parent = build / "stage" / config
    stage_parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="package-", dir=stage_parent) as temporary:
        work = Path(temporary)
        stage, output = work / "app0", work / "pkg"
        output.mkdir()
        for source, target in inputs:
            destination = stage / target
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, destination)
            if sha256(destination) != manifest["files"][target]["sha256"]:
                raise PackageError(f"Input changed during package staging: {target}")
        sfo = stage / "sce_sys/param.sfo"
        sfo.write_bytes(encode_sfo(metadata))
        manifest["files"]["sce_sys/param.sfo"] = {"size": sfo.stat().st_size, "sha256": sha256(sfo)}
        manifest_path = stage / "package-manifest.json"
        manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        targets = sorted([*manifest["files"], "package-manifest.json"])
        project = work / "kstar_gfx.gp4"
        write_gp4(project, stage, metadata, targets)
        print(f"Packaging {config}: {len(targets)} files", flush=True)
        creation_dates = {datetime.now(timezone.utc).strftime("%Y%m%d")}
        build_log = run_tool(tool, "pkg_build", project, output)
        creation_dates.add(datetime.now(timezone.utc).strftime("%Y%m%d"))
        candidate = output / (metadata["content_id"] + ".pkg")
        regular_file(candidate)
        with candidate.open("rb") as stream:
            if stream.read(4) != b"\x7fCNT":
                raise PackageError("PkgTool output does not have a PS4 package header")
        validation = run_tool(tool, "pkg_validate", candidate)
        if "[OK]" not in validation or "[WARN]" in validation:
            raise PackageError("Package validation was incomplete:\n" + validation)
        extracted = work / "extracted"
        payload, extraction_log = extract_package(tool, candidate, extracted)
        packaged_sfo = validate_packaged_sfo(sfo, payload / "sce_sys/param.sfo",
                                           candidate.stat().st_size, creation_dates)
        for target in targets:
            if target == "sce_sys/param.sfo":
                continue
            path = payload / target
            regular_file(path)
            if path.stat().st_size != (stage / target).stat().st_size or sha256(path) != sha256(stage / target):
                raise PackageError(f"Extracted package bytes differ from staged input: {target}")
        package_hash = sha256(candidate)
        result = {"schema": 1, "package": candidate.name, "sha256": package_hash,
                  "bytes": candidate.stat().st_size, "verified_payload_files": len(targets),
                  "byte_exact_files": len(targets) - 1, "packaged_sfo": packaged_sfo,
                  "extracted_files": len(inventory(payload)), "configuration": config,
                  "pkg_validate_ok": validation.count("[OK]"), "manifest": manifest}
        candidate.with_suffix(".manifest.json").write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        candidate.with_suffix(".validation.txt").write_text(
            version + "\n\n" + build_log + "\n" + validation + "\n" + extraction_log
            + f"\nVerified {len(targets) - 1} byte-exact files against staged SHA256 hashes.\n"
            + "Validated all original SFO fields and the two LibOrbisPkg publishing additions.\n"
            + f"Packaged SFO SHA256 {packaged_sfo['sha256']}\n", encoding="utf-8")
        candidate.with_suffix(".pkg.sha256").write_text(package_hash + "  " + candidate.name + "\n", encoding="ascii")
        destination = build / "pkg" / config
        destination.mkdir(parents=True, exist_ok=True)
        for artifact in sorted(output.iterdir(), key=lambda path: path == candidate):
            os.replace(artifact, destination / artifact.name)
        published = destination / candidate.name
        print(f"Created {published}\nSHA256 {package_hash}", flush=True)
        return published


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--config", choices=("Debug", "Release"), default="Debug")
    parser.add_argument("--build-dir", type=Path, help="existing GNM CMake build directory")
    parser.add_argument("--sdk", type=Path, help="OpenOrbis SDK (default: environment or selected CMake cache)")
    parser.add_argument("--pkg-tool", type=Path, help="host-compatible LibOrbisPkg executable")
    args = parser.parse_args(argv)
    root = Path(__file__).resolve().parents[1]
    try:
        package(root, args.build_dir or root / "build/ps4-x86_64/standalone", args.config, args.sdk, args.pkg_tool)
    except (PackageError, OSError, ValueError, KeyError, TypeError) as error:
        print(f"PS4 packaging error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
