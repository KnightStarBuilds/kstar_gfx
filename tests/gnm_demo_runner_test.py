#!/usr/bin/env python3
"""Isolation and input preservation for the visible shadPS4 launch workflow."""
import hashlib
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import run_gnm_demo as runner


class Profiles(unittest.TestCase):
    def test_imported_accounts_and_bindings_stay_isolated(self):
        (ROOT / ".tmp").mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=ROOT / ".tmp") as temporary:
            source = Path(temporary) / "source profile"
            destination = Path(temporary) / "new output/user"
            (source / "input_config").mkdir(parents=True)
            (source / "config.json").write_text(json.dumps({"General": {"home_dir": "/original/home"}, "GPU": {"screen_width": 1280, "copy_gpu_buffers": False}}))
            (source / "users.json").write_text(json.dumps({"Users": {"user": [{"user_id": 42}]}}))
            (source / "input_config/default.ini").write_text("options = enter\ntriangle = c\n")
            (source / "input_config/custom.ini").write_text("cross = n\n")
            original = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in source.rglob("*") if p.is_file()}
            settings = runner.prepare_profile(destination, runner.profile_inputs(source), headless=False)
            self.assertEqual(settings["user_ids"], [42, 1000, 1001, 1002, 1003])
            config = json.loads((destination / "config.json").read_text())
            self.assertEqual(config["General"]["home_dir"], str(destination / "home"))
            self.assertEqual(config["GPU"]["screen_width"], 1280)
            self.assertFalse(config["GPU"]["copy_gpu_buffers"])
            self.assertEqual(config["GPU"]["readbacks_mode"], 2)
            self.assertTrue((destination / "home/42/savedata").is_dir())
            self.assertIn("options = enter", (destination / "input_config/default.ini").read_text())
            self.assertIn("options = escape", (destination / "input_config/default.ini").read_text())
            self.assertIn("cross = n", (destination / "input_config/custom.ini").read_text())
            self.assertEqual(original, {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in original})

    def test_invalid_profiles_rejected_before_copy(self):
        with tempfile.TemporaryDirectory(dir=ROOT / ".tmp") as temporary:
            source = Path(temporary)
            (source / "users.json").write_text('{"Users":{"user":[{"user_id":-1}]}}')
            with self.assertRaises(runner.ValidationError):
                runner.profile_inputs(source)
            (source / "users.json").unlink()
            (source / "config.json").symlink_to(source / "missing.json")
            with self.assertRaises(runner.ValidationError):
                runner.profile_inputs(source)

    def test_new_profile_has_native_quit_and_controller_bindings(self):
        with tempfile.TemporaryDirectory(dir=ROOT / ".tmp") as temporary:
            profile = Path(temporary) / "user"
            runner.prepare_profile(profile, [], headless=False)
            text = (profile / "input_config/default.ini").read_text()
            for binding in ("options = escape", "options = options", "triangle = c", "axis_left_x = axis_left_x"):
                self.assertIn(binding, text)


class LaunchDuration(unittest.TestCase):
    def test_manual_quit_has_no_hidden_deadline_and_validation_stays_bounded(self):
        # Exercise the actual launcher with a process double: its command and
        # wait call must both be untimed, not just the guest's scene options.
        cases = (([], None, None, None),
                 (["--seconds", "7"], 127.0, None, "7.0"),
                 (["--headless"], 120.0, "4", None))
        (ROOT / ".tmp").mkdir(exist_ok=True)
        for extra, deadline, frames, seconds in cases:
            with self.subTest(arguments=extra), tempfile.TemporaryDirectory(dir=ROOT / ".tmp") as temporary:
                base = Path(temporary)
                sdk, resources, output = base / "sdk", base / "resources", base / "output"
                converter = {"darwin": "bin/macos/create-fself-macos", "linux": "bin/linux/create-fself", "win32": "bin/windows/create-fself.exe"}[sys.platform]
                for relative in ("lib/crt1.o", converter, "samples/hello_world/sce_module/libc.prx", "samples/hello_world/sce_module/libSceFios2.prx"):
                    path = sdk / relative
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(b"fixture")
                    path.chmod(0o755)
                (resources / "shaders").mkdir(parents=True)
                (resources / "shaders/triangle.vert.sb").write_bytes(b"fixture")
                executable = base / "executable"
                executable.write_bytes(b"fixture")
                executable.chmod(0o755)
                def package(command, **kwargs):
                    Path(command[command.index("--eboot") + 1]).write_bytes(b"fixture")
                class Process:
                    pid = 12345
                    def __init__(self, command, **kwargs):
                        self.command = command
                        kwargs["stdout"].write(b"kstar_gfx GNM / fixture\nGNM_PRESENT_PROGRESS submitted=1 completed=1\n")
                    def wait(self, timeout=None):
                        self.timeout = timeout
                        return 0
                launched = []
                def launch(command, **kwargs):
                    process = Process(command, **kwargs)
                    launched.append(process)
                    return process
                arguments = ["--emulator", str(executable), "--elf", str(executable), "--sdk", str(sdk), "--resources", str(resources), "--output", str(output), *extra]
                with patch.object(runner.subprocess, "run", side_effect=package), patch.object(runner.subprocess, "Popen", side_effect=launch), contextlib.redirect_stdout(io.StringIO()):
                    runner.main(arguments, presentation_default=True)
                self.assertEqual(len(launched), 1)
                self.assertEqual(launched[0].timeout, deadline)
                result = json.loads((output / "result.json").read_text())
                self.assertEqual(result["timeout_seconds"], deadline)
                for flag, expected in (("--frames", frames), ("--seconds", seconds)):
                    if expected is None:
                        self.assertNotIn(flag, result["command"])
                    else:
                        self.assertEqual(result["command"][result["command"].index(flag) + 1], expected)


if __name__ == "__main__":
    unittest.main()
