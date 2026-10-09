#!/usr/bin/env python3
"""GPU output checks: analytic triangle, visible 3D scenes, deterministic frames.

No self-generated image is accepted as a visual reference. The triangle checks
known interpolation/clear values; scene checks catch empty/flat output and
repeat captures expose uninitialized or frame-dependent behavior.
"""
import argparse
import json
import pathlib
import subprocess
import tempfile


def read_ppm(path):
    with path.open("rb") as stream:
        if stream.readline() != b"P6\n":
            raise AssertionError("capture is not a binary PPM")
        width, height = map(int, stream.readline().split())
        if stream.readline() != b"255\n":
            raise AssertionError("capture is not 8-bit RGB")
        pixels = stream.read()
    assert len(pixels) == width * height * 3
    return width, height, pixels


def pixel(image, x, y):
    width, _, data = image
    i = (y * width + x) * 3
    return tuple(data[i:i + 3])


def validate_triangle(image):
    width, height, _ = image
    corner = pixel(image, 4, 4)
    assert all(abs(a - b) <= 2 for a, b in zip(corner, (6, 8, 11))), corner
    # Evaluate barycentric colors at pixel centers in the portable clip space.
    vertices = ((-.7, -.65), (.7, -.65), (0., .75))
    for px, py in ((width // 2, height // 2),
                   (width // 3, height * 2 // 3),
                   (width * 2 // 3, height * 2 // 3),
                   (width // 2, height // 3)):
        x = (px + .5) * 2 / width - 1
        y = 1 - (py + .5) * 2 / height
        (x0, y0), (x1, y1), (x2, y2) = vertices
        denominator = (y1-y2)*(x0-x2)+(x2-x1)*(y0-y2)
        red = ((y1-y2)*(x-x2)+(x2-x1)*(y-y2))/denominator
        green = ((y2-y0)*(x-x2)+(x0-x2)*(y-y2))/denominator
        expected = (red*255, green*255, (1-red-green)*255)
        observed = pixel(image, px, py)
        assert all(abs(a-b) <= 3 for a, b in zip(observed, expected)), (observed, expected)


def validate_scene(image):
    width, height, data = image
    colors = {pixel(image, x, y) for y in range(0, height, 3)
              for x in range(0, width, 3)}
    assert len(colors) > 128, f"scene appears flat ({len(colors)} sampled colors)"
    assert max(data)-min(data) > 100, "scene lacks expected color range"
    # Center and corners should not all contain only the environment fixture.
    assert len({pixel(image, x, y) for y in (height//3, height//2, 2*height//3)
                for x in (width//3, width//2, 2*width//3)}) > 3


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=pathlib.Path)
    parser.add_argument("--resources", type=pathlib.Path)
    parser.add_argument("--scene", default="triangle",
                        choices=("triangle", "cubes", "instances", "offscreen", "stress", "pbr"))
    parser.add_argument("--scratch", type=pathlib.Path, required=True)
    parser.add_argument("--environment", choices=("0", "1"), default="0")
    parser.add_argument("--view", choices=("overview", "rifle", "hat"), default="overview")
    args = parser.parse_args()
    args.scratch = args.scratch.resolve()
    args.scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=args.scratch, prefix="gpu-capture-") as temp:
        captures = []
        for index in range(2):
            output = pathlib.Path(temp)/f"capture {index}.ppm"
            command = [str(args.executable.resolve()), "--demo", args.scene,
                       "--frames", "4", "--width", "320", "--height", "180",
                       "--environment", args.environment, "--view", args.view,
                       "--headless", "--output", str(output)]
            if args.resources:
                command += ["--resources", str(args.resources.resolve())]
            subprocess.run(command, check=True, timeout=120, cwd=temp)
            image = read_ppm(output)
            assert image[:2] == (320, 180), image[:2]
            (validate_triangle if args.scene == "triangle" else validate_scene)(image)
            metadata = json.loads(output.with_suffix(".json").read_text())
            assert metadata["capture_source"] == "completed GPU LDR render target"
            assert metadata["frames"] == metadata["capture_frame"] == 4
            assert metadata["stats"]["draws"] > 0
            assert metadata["stats"]["steady_frame_allocations"] == 0
            if args.scene != "stress":
                assert metadata["stats"]["steady_frames"] == 3
            captures.append(image[2])
        assert max(abs(a-b) for a, b in zip(*captures)) <= 1, "repeat capture is not deterministic"
    print(f"{args.scene}: analytic/visibility and repeat-output checks passed")


if __name__ == "__main__":
    main()
