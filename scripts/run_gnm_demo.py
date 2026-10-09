#!/usr/bin/env python3
"""Run a shared GNM demo in an isolated shadPS4 profile.

Headless runs check completed GPU readbacks. Visible runs also require completed
VideoOut flips; host-window content and input response need separate observation.
Inputs, logs, settings, captures and results remain in a new evidence directory.
"""
import argparse
import hashlib
import json
import math
import os
import re
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]


class ValidationError(RuntimeError):
    pass


class WindowClosed(RuntimeError):
    pass


class InterruptedRun(RuntimeError):
    def __init__(self, signum):
        self.signum = signum


def write_result(output, result):
    temporary = output / "result.json.tmp"
    temporary.write_text(json.dumps(result, indent=2)+"\n")
    os.replace(temporary, output / "result.json")


def configured_sdk(build):
    value = os.environ.get("OO_PS4_TOOLCHAIN")
    if value:
        return Path(value)
    cache = build / "CMakeCache.txt"
    if cache.is_file():
        for line in cache.read_text().splitlines():
            if line.startswith("OO_PS4_TOOLCHAIN:") and "=" in line:
                return Path(line.split("=", 1)[1])
    return None


def profile_inputs(source):
    """Copy settings/accounts/input only, never source saves or writable caches."""
    if source is None:
        return []
    if source.is_symlink() or not source.is_dir():
        raise ValidationError("Emulator profile must be a regular directory")
    inputs = []
    for name in ("config.json", "users.json", "input_config"):
        path = source / name
        if not path.exists() and not path.is_symlink():
            continue
        candidates = [path, *path.rglob("*")] if path.is_dir() else [path]
        for item in candidates:
            if item.is_symlink() or not (item.is_file() or item.is_dir()):
                raise ValidationError("Profile inputs must be regular files/directories: "+str(item))
            if item.is_file():
                inputs.append((item, item.relative_to(source)))
    # Parse settings before any output is created.
    for path, relative in inputs:
        if relative.name in ("config.json", "users.json"):
            if not isinstance(json.loads(path.read_text()), dict):
                raise ValidationError(str(relative)+" must contain a JSON object")
    profile_settings(source)
    return inputs


def profile_settings(profile):
    path = profile / "config.json"
    config = json.loads(path.read_text()) if path.exists() else {}
    for key in ("General", "GPU", "Debug"):
        if not isinstance(config.setdefault(key, {}), dict):
            raise ValidationError("Emulator configuration section must be an object: "+key)
    user_ids = {1000, 1001, 1002, 1003}
    users_path = profile / "users.json"
    if users_path.exists():
        users = json.loads(users_path.read_text())
        if not isinstance(users.get("Users", {}), dict):
            raise ValidationError("Emulator users.json must contain a Users object")
        accounts = users.get("Users", {}).get("user", [])
        if not isinstance(accounts, list):
            raise ValidationError("Emulator Users.user must be a list")
        for account in accounts:
            user_id = account.get("user_id") if isinstance(account, dict) else None
            if type(user_id) is not int or not 0 <= user_id <= 0x7fffffff:
                raise ValidationError("Emulator user_id must be a nonnegative signed 32-bit integer")
            user_ids.add(user_id)
    return config, user_ids


def prepare_profile(profile, inputs, *, headless):
    for source, relative in inputs:
        destination = profile / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
    config, user_ids = profile_settings(profile)
    path = profile / "config.json"
    home = profile / "home"
    for uid in sorted(user_ids):
        for sub in ("savedata", "trophy", "inputs"):
            (home/str(uid)/sub).mkdir(parents=True, exist_ok=True)
    config["General"].update(home_dir=str(home), show_splash=False,
                             enable_upnp=False, connected_to_network=False)
    config["GPU"].update(dump_shaders=headless,
                         readback_linear_images_enabled=True, readbacks_mode=2,
                         null_gpu=False, rcas_enabled=False)
    config["Debug"].update(debug_dump=headless)
    path.write_text(json.dumps(config, indent=2)+"\n")
    if not headless:
        default_input = profile / "input_config/default.ini"
        default_input.parent.mkdir(parents=True, exist_ok=True)
        if not default_input.exists():
            shutil.copyfile(ROOT/"ps4/input-default.ini", default_input)
        for mapping in default_input.parent.glob("*.ini"):
            if mapping.name == "global.ini":
                continue
            text = mapping.read_text()
            if not re.search(r"^\s*options\s*=\s*escape\s*(?:#.*)?$", text, re.MULTILINE):
                mapping.write_text(text.rstrip()+"\n\n# kstar_gfx: Escape quits through the native Options action.\noptions = escape\n")
    return {"path": str(home), "user_ids": sorted(user_ids),
            "configuration_sha256": sha256(path)}


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_ppm(path):
    with path.open("rb") as stream:
        if stream.readline() != b"P6\n":
            raise ValidationError("GPU capture is not binary PPM")
        width, height = map(int, stream.readline().split())
        if not 1 <= width <= 16384 or not 1 <= height <= 16384:
            raise ValidationError("Invalid capture dimensions")
        if stream.readline() != b"255\n":
            raise ValidationError("Capture does not use 8-bit RGB")
        pixels = stream.read()
    if len(pixels) != width * height * 3:
        raise ValidationError("Truncated GPU capture")
    return width, height, pixels


def validate_pixels(image, scene, diagnostic="shaded"):
    width, height, pixels = image
    def pixel(x, y):
        offset = (y * width + x) * 3
        return pixels[offset:offset + 3]
    if scene == "triangle":
        if any(abs(a-b) > 3 for a, b in zip(pixel(4, 4), (6, 8, 11))):
            raise ValidationError("Triangle clear color is wrong")
        vertices = ((-.7, -.65), (.7, -.65), (0., .75))
        for x, y in ((width//2, height//2), (width//3, 2*height//3),
                     (2*width//3, 2*height//3), (width//2, height//3)):
            px, py = (x+.5)*2/width-1, 1-(y+.5)*2/height
            (x0, y0), (x1, y1), (x2, y2) = vertices
            denominator = (y1-y2)*(x0-x2)+(x2-x1)*(y0-y2)
            red = ((y1-y2)*(px-x2)+(x2-x1)*(py-y2))/denominator
            green = ((y2-y0)*(px-x2)+(x0-x2)*(py-y2))/denominator
            expected = (255*red, 255*green, 255*(1-red-green))
            if any(abs(a-b) > 4 for a, b in zip(pixel(x, y), expected)):
                raise ValidationError("Triangle interpolation/orientation mismatch")
    else:
        colors = {bytes(pixel(x, y)) for y in range(0, height, 3)
                  for x in range(0, width, 3)}
        minimum_colors, minimum_range = (128, 100) if diagnostic == "shaded" else (8, 32)
        if len(colors) <= minimum_colors or max(pixels)-min(pixels) <= minimum_range:
            raise ValidationError("3D GPU output is empty or lacks expected color variation")
    return {"status": "passed", "kind": "analytic triangle" if scene == "triangle"
            else "visible 3D " + diagnostic + " output", "width": width, "height": height}


def compare_images(actual, reference):
    if actual[:2] != reference[:2]:
        raise ValidationError("Reference dimensions differ from capture")
    width, height, pixels = actual
    errors = [abs(a-b) for a, b in zip(pixels, reference[2])]
    mae = sum(errors)/(len(errors)*255)
    sorted_errors = sorted(errors)
    p95 = sorted_errors[min(len(errors)-1, int(len(errors)*.95))]/255
    large_fraction = sum(error > 32 for error in errors)/len(errors)
    def worst_tile_error(grid):
        worst = 0.
        for ty in range(grid):
            for tx in range(grid):
                total = count = 0
                for y in range(ty*height//grid, (ty+1)*height//grid):
                    start = (y*width+tx*width//grid)*3
                    end = (y*width+(tx+1)*width//grid)*3
                    total += sum(errors[start:end])
                    count += end-start
                worst = max(worst, total/(count*255))
        return worst
    worst_tile = worst_tile_error(4)
    # Fine tiles expose localized transparency/shadow defects that can hide
    # inside a passing whole-image average or a large background region.
    worst_fine_tile = worst_tile_error(16)
    passed = (mae <= .025 and p95 <= .12 and large_fraction <= .08
              and worst_tile <= .08 and worst_fine_tile <= .08)
    return {"status": "passed" if passed else "failed", "mean_absolute_error": mae,
            "p95_absolute_error": p95, "channels_over_32_fraction": large_fraction,
            "worst_4x4_tile_mean_error": worst_tile,
            "worst_16x16_tile_mean_error": worst_fine_tile,
            "limits": {"mean": .025, "p95": .12, "channels_over_32_fraction": .08,
                       "worst_4x4_tile_mean": .08, "worst_16x16_tile_mean": .08}}


def stop_owned_process(process):
    if process.poll() is not None:
        return process.returncode
    if os.name == "nt":
        # The directly launched emulator owns this Windows process group.
        # CTRL_BREAK is graceful when a console exists; taskkill also reaps
        # descendants if the console-less emulator cannot receive the signal.
        try:
            process.send_signal(signal.CTRL_BREAK_EVENT)
            return process.wait(timeout=5)
        except (OSError, subprocess.TimeoutExpired):
            pass
        if process.poll() is None:
            try:
                subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                               timeout=5, check=False)
            except (OSError, subprocess.SubprocessError):
                pass
        if process.poll() is None:
            process.kill()
        try:
            return process.wait(timeout=5)
        except subprocess.TimeoutExpired as error:
            raise ValidationError("Owned emulator process did not terminate") from error
    # A new session makes this process group exclusive to this validation run.
    for sig in (signal.SIGTERM, signal.SIGKILL):
        try:
            os.killpg(process.pid, sig)
        except ProcessLookupError:
            pass
        try:
            return process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            pass
    raise ValidationError("Owned emulator process did not terminate")


def main(argv=None, *, presentation_default=False):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("emulator", "output"):
        parser.add_argument("--"+name, required=True, type=Path)
    for name in ("elf", "sdk", "resources", "emulator-profile"):
        parser.add_argument("--"+name, type=Path)
    parser.add_argument("--config", choices=("Debug", "Release"), default="Debug")
    parser.add_argument("--build-dir", type=Path, default=ROOT/"build/ps4-x86_64/standalone")
    presentation = parser.add_mutually_exclusive_group()
    presentation.add_argument("--headless", dest="headless", action="store_true",
                              help="Disable guest VideoOut and validate offscreen GPU captures")
    presentation.add_argument("--present", dest="headless", action="store_false",
                              help="Show the native VideoOut output and enable controller input")
    parser.set_defaults(headless=not presentation_default)
    parser.add_argument("--scene", default="pbr" if presentation_default else "triangle", choices=("triangle", "cubes", "pbr", "instances", "offscreen", "stress"))
    parser.add_argument("--view", default="overview", choices=("overview", "rifle", "hat"))
    parser.add_argument("--environment", type=int, choices=(0, 1), default=0)
    diagnostics = ("shaded", "normals", "albedo", "material", "shadows")
    parser.add_argument("--diagnostic", choices=diagnostics, default="shaded")
    duration = parser.add_mutually_exclusive_group()
    duration.add_argument("--frames", type=int)
    duration.add_argument("--seconds", type=float, help="Opt-in visible session duration after scene loading; otherwise quit manually")
    parser.add_argument("--width", type=int)
    parser.add_argument("--height", type=int)
    parser.add_argument("--timeout", type=float,
                        help="Process deadline allowance in seconds (default: 120 for bounded runs; none for interactive runs)")
    parser.add_argument("--reference", type=Path)
    parser.add_argument("--icd", type=Path, help="Explicit Vulkan ICD manifest for driver isolation")
    args = parser.parse_args(argv)
    args.build_dir = args.build_dir.expanduser().resolve()
    args.elf = args.elf or args.build_dir/"bin"/args.config/"kstar_gfx_demo"
    args.resources = args.resources or args.build_dir/"resources"/args.config
    args.sdk = args.sdk or configured_sdk(args.build_dir)
    if args.sdk is None:
        parser.error("Build GNM first, set OO_PS4_TOOLCHAIN, or pass --sdk")
    if args.headless and args.frames is None and args.seconds is None:
        args.frames = 4
    if args.timeout is None and (args.frames is not None or args.seconds is not None):
        args.timeout = 120.
    args.width = args.width if args.width is not None else (320 if args.headless else 1920)
    args.height = args.height if args.height is not None else (180 if args.headless else 1080)
    if sys.platform not in ("darwin", "linux", "win32"):
        parser.error("The isolated shadPS4 runner supports macOS, Linux and Windows")
    for name in ("emulator", "elf", "sdk", "resources", "output"):
        if name == "output" and getattr(args, name).expanduser().is_symlink():
            parser.error("Output must be a new regular directory, not a symlink")
        setattr(args, name, getattr(args, name).expanduser().resolve())
    inputs = []
    if args.emulator_profile:
        args.emulator_profile = args.emulator_profile.expanduser().absolute()
        try:
            inputs = profile_inputs(args.emulator_profile)
        except (ValidationError, ValueError, OSError) as error:
            parser.error(str(error))
    if not args.emulator.is_file() or not os.access(args.emulator, os.X_OK):
        parser.error("--emulator must be an executable file")
    if not args.elf.is_file() or not (args.sdk/"lib/crt1.o").is_file():
        parser.error("Missing ELF or OpenOrbis SDK")
    if not (args.resources/"shaders/triangle.vert.sb").is_file():
        parser.error("Resources must contain compiled PS4 .sb shader artifacts")
    if args.output.exists():
        parser.error("Output must be new; retained evidence is never overwritten")
    if any(args.output == item or item in args.output.parents or args.output in item.parents
           for item in (args.resources, args.sdk, args.emulator_profile) if item):
        parser.error("Output must be separate from SDK, resource inputs and source profiles")
    if (args.frames is not None and not 1 <= args.frames <= 100000) or not 16 <= args.width <= 16384 or not 16 <= args.height <= 16384:
        parser.error("Invalid bounded frame count or capture dimensions")
    if args.seconds is not None and (args.headless or not math.isfinite(args.seconds) or not 0 < args.seconds <= 600):
        parser.error("--seconds requires a visible session and a duration within 0..600")
    if args.timeout is not None and (not math.isfinite(args.timeout) or not 1 <= args.timeout <= 3600):
        parser.error("Timeout must be between 1 and 3600 seconds")
    if args.view != "overview" and args.scene != "pbr":
        parser.error("Close views require the PBR scene")
    if args.reference and (not args.headless or args.frames is None):
        parser.error("Reference comparisons require deterministic --headless --frames runs")
    resize_extra = 16 if args.scene == "stress" and args.frames and ((args.frames-1)//60)%2 else 0
    expected_dimensions = (args.width+resize_extra, args.height+resize_extra)
    reference = None
    if args.reference:
        args.reference = args.reference.expanduser().resolve()
        reference = read_ppm(args.reference)
        if reference[:2] != expected_dimensions:
            parser.error("Reference image dimensions differ from requested output")
    converters = {"darwin": "bin/macos/create-fself-macos",
                  "linux": "bin/linux/create-fself",
                  "win32": "bin/windows/create-fself.exe"}
    converter = args.sdk/converters[sys.platform]
    if not converter.is_file() or not os.access(converter, os.X_OK):
        parser.error("SDK FSELF converter is missing")
    runtime_names = ("libc.prx", "libSceFios2.prx")
    runtime_directory = args.sdk/"samples/hello_world/sce_module"
    if any(not (runtime_directory/name).is_file() for name in runtime_names):
        parser.error("SDK demo runtime modules are missing")
    driver = None
    if args.icd:
        args.icd = args.icd.expanduser().resolve()
        if not args.icd.is_file():
            parser.error("Vulkan ICD manifest is missing")
        try:
            description = json.loads(args.icd.read_text())
            library = Path(description["ICD"]["library_path"])
            if not library.is_absolute():
                library = args.icd.parent/library
            driver = {"manifest": str(args.icd), "manifest_sha256": sha256(args.icd),
                      "api_version": description["ICD"].get("api_version"),
                      "library": str(library)}
            if library.is_file():
                driver["library_sha256"] = sha256(library)
        except (ValueError, KeyError, OSError) as error:
            parser.error("Invalid Vulkan ICD manifest: "+str(error))
    args.output.mkdir(parents=True)
    result = {"status": "preparing", "scope": "shared GNM offscreen GPU output" if args.headless else "shared GNM presentation session",
              "configuration": args.config, "headless": args.headless,
              "scene": args.scene, "view": args.view, "environment": args.environment,
              "diagnostic": args.diagnostic,
              "emulator": str(args.emulator), "emulator_sha256": sha256(args.emulator),
              "elf": str(args.elf), "elf_sha256": sha256(args.elf),
              "width": args.width, "height": args.height, "frames": args.frames,
              "seconds": args.seconds, "timeout_seconds": None if args.timeout is None else args.timeout + (args.seconds or 0), "readbacks_mode": 2,
              "emulator_profile_source": str(args.emulator_profile) if args.emulator_profile else None,
              "observations": {"window_content": "not automatically assessed", "controller_response": "not automatically assessed"},
              "native_ps4_performance": "not measured", "native_ps4_execution": "not tested"}
    started = time.monotonic()
    previous_handlers = {}
    env = os.environ.copy()
    env["OO_PS4_TOOLCHAIN"] = str(args.sdk)
    if args.icd:
        env["VK_ICD_FILENAMES"] = str(args.icd)
        env["VK_DRIVER_FILES"] = str(args.icd)
        result["vulkan_driver"] = driver
    else:
        result["vulkan_driver"] = {"manifest": "emulator/default environment",
                                  "VK_ICD_FILENAMES": env.get("VK_ICD_FILENAMES"),
                                  "VK_DRIVER_FILES": env.get("VK_DRIVER_FILES")}
    write_result(args.output, result)
    try:
        app = args.output/"app0"
        (app/"sce_module").mkdir(parents=True)
        for name in runtime_names:
            shutil.copy2(runtime_directory/name, app/"sce_module"/name)
        with (args.output/"package.log").open("wb") as log:
            subprocess.run([str(converter), "-in="+str(args.elf), "-out="+str(app/"demo.oelf"),
                            "--eboot", str(app/"eboot.bin"), "--paid", "0x3800000000000011"],
                           env=env, check=True, stdout=log, stderr=subprocess.STDOUT,
                           timeout=30)
        shutil.copytree(args.resources, app/"resources", copy_function=shutil.copy2)
        result["resource_files"] = {str(path.relative_to(app/"resources")):
                                    {"size": path.stat().st_size, "sha256": sha256(path)}
                                    for path in sorted((app/"resources").rglob("*")) if path.is_file()}
        profile = args.output/"user"
        captures = profile/"data/captures"
        captures.mkdir(parents=True)
        result["emulator_home"] = prepare_profile(profile, inputs, headless=args.headless)
        result["profile_inputs"] = {str(relative): sha256(source) for source, relative in inputs}
        result["app0"] = str(app)
        result["eboot_sha256"] = sha256(app/"eboot.bin")
        capture_name = f"{args.scene}-{args.view}-e{args.environment}.ppm"
        command = [str(args.emulator), "-g", str(app), "--", "kstar_gfx_demo",
                   "--demo", args.scene, "--view", args.view,
                   "--environment", str(args.environment), "--diagnostic", args.diagnostic,
                   "--width", str(args.width), "--height", str(args.height),
                   "--output", "/data/captures/"+capture_name]
        if args.frames:
            command += ["--frames", str(args.frames), "--capture-frame", str(args.frames)]
        elif args.seconds is not None:
            command += ["--seconds", str(args.seconds)]
        if args.headless:
            command.append("--headless")
        result["command"] = command
        # A native Python exec keeps ownership observable while Rosetta starts
        # an x86_64 emulator; the following exec preserves this process PID.
        if os.name == "nt":
            launcher = command
            process_options = {"creationflags": subprocess.CREATE_NEW_PROCESS_GROUP}
        else:
            launcher = [sys.executable, "-c", "import os,sys;os.execv(sys.argv[1],sys.argv[1:])", *command]
            process_options = {"start_new_session": True}
        result["launcher_command"] = launcher
        def interrupted(signum, _frame):
            raise InterruptedRun(signum)
        for signum in (signal.SIGINT, signal.SIGTERM):
            previous_handlers[signum] = signal.signal(signum, interrupted)
        print(f"Launching {args.config} {args.scene} ({'offscreen' if args.headless else 'visible'}); evidence: {args.output}", flush=True)
        with (args.output/"emulator.log").open("wb") as log:
            process = subprocess.Popen(launcher, cwd=args.output, env=env,
                                       stdout=log, stderr=subprocess.STDOUT, **process_options)
            result.update(status="running", pid=process.pid)
            write_result(args.output, result)
            try:
                result["returncode"] = process.wait(timeout=result["timeout_seconds"])
            except subprocess.TimeoutExpired:
                result["timed_out"] = True
                result["returncode"] = stop_owned_process(process)
            except BaseException:
                stop_owned_process(process)
                raise
        transcript = re.sub(r"\x1b\[[0-9;]*m", "", (args.output/"emulator.log").read_text(errors="replace"))
        result["guest_messages"] = [line for line in transcript.splitlines()
                                    if "[stdout]" in line or "[stderr]" in line][-24:]
        if result.get("timed_out") or result["returncode"] != 0:
            raise ValidationError("Emulator timed out or exited unsuccessfully")
        captured = captures/capture_name
        metadata_path = captured.with_suffix(".json")
        if not captured.is_file() or not metadata_path.is_file():
            if (not args.headless and "kstar_gfx GNM /" in transcript
                    and re.search(r"GNM_PRESENT(?:_PROGRESS)? .*completed=[1-9]\d*", transcript)):
                raise WindowClosed("Closed without the final capture; validation remains incomplete")
            raise ValidationError("Completed GPU capture or metadata is missing")
        image = read_ppm(captured)
        metadata = json.loads(metadata_path.read_text())
        if args.scene == "stress" and args.seconds:
            expected_dimensions = (args.width+16, args.height+16) if ((metadata.get("capture_frame", 1)-1)//60)%2 else (args.width, args.height)
        if image[:2] != expected_dimensions:
            raise ValidationError("GPU capture has unexpected dimensions")
        if metadata.get("capture_source") != "completed GPU LDR render target":
            raise ValidationError("Capture does not report completed GPU readback")
        checks = [("demo", args.scene), ("view", args.view)]
        if args.frames:
            checks += [("frames", args.frames), ("capture_frame", args.frames)]
        if args.headless:
            checks += [("environment", args.environment), ("diagnostic", diagnostics.index(args.diagnostic))]
        else:
            checks += [("presentation_enabled", True)]
        for key, expected in checks:
            if metadata.get(key) != expected:
                raise ValidationError("Capture metadata mismatch: "+key)
        if "GNM" not in metadata.get("backend", "").upper():
            raise ValidationError("Capture was not rendered through the GNM backend")
        result["capture"] = {"path": str(captured.relative_to(args.output)),
                             "sha256": sha256(captured), "metadata": metadata}
        if not args.headless:
            match = re.search(r"GNM_PRESENT submitted=(\d+) completed=(\d+)", transcript)
            if not match or int(match[1]) < 1 or int(match[2]) != int(match[1]):
                raise ValidationError("Completed VideoOut presentation summary is missing or incomplete")
            result["presentation"] = {"status": "completed flips", "submitted": int(match[1]), "completed": int(match[2]),
                                      "window_content": "requires host-window observation"}
        try:
            result["pixel_checks"] = validate_pixels(image, args.scene, diagnostics[metadata.get("diagnostic", 0)])
        except ValidationError as error:
            result["pixel_checks"] = {"status": "failed", "error": str(error)}
        if reference:
            result["reference"] = {"path": str(args.reference), "sha256": sha256(args.reference)}
            result["comparison"] = compare_images(image, reference)
        else:
            result["comparison"] = {"status": "not requested"}
        if result["pixel_checks"]["status"] != "passed":
            raise ValidationError(result["pixel_checks"]["error"])
        if result["comparison"]["status"] == "failed":
            raise ValidationError("GNM GPU output differs from the OpenGL reference")
        result["status"] = "passed"
    except WindowClosed as error:
        result.update(status="closed", reason=str(error))
    except InterruptedRun as error:
        result.update(status="interrupted", interrupted_signal=error.signum)
    except (ValidationError, OSError, ValueError, subprocess.SubprocessError) as error:
        result["status"] = "failed"
        result["error"] = str(error)
    finally:
        for signum, handler in previous_handlers.items():
            signal.signal(signum, handler)
        result["elapsed_seconds"] = time.monotonic()-started
        write_result(args.output, result)
    print(json.dumps({key: value for key, value in result.items() if key != "resource_files"}, indent=2))
    return 0 if result["status"] in ("passed", "closed") else 1


if __name__ == "__main__":
    raise SystemExit(main())
