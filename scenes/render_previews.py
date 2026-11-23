#!/usr/bin/env python3
"""Render benchmark images, record timings, and write a plain HTML gallery."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime, timezone
import html
import json
import math
from pathlib import Path
import re
import struct
import subprocess
import zlib

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("binary", type=Path, nargs="?")
parser.add_argument("--size", type=int, default=512)
parser.add_argument("--frames", type=int, default=4, help="minimum measured frames per scene (at least 4)")
parser.add_argument("--min-seconds", type=float, default=10, help="minimum measured seconds per scene (at least 10)")
parser.add_argument("--warmup", type=int, default=2)
parser.add_argument("--jobs", type=int, default=1)
parser.add_argument("--resume", action="store_true", help="reuse completed images with matching settings")
parser.add_argument("--html-only", action="store_true", help="rebuild the gallery from saved metrics without rendering")
args = parser.parse_args()
if (not 512 <= args.size <= 16384 or args.frames < 4
        or not math.isfinite(args.min_seconds) or args.min_seconds < 10
        or not 0 <= args.warmup <= 100000 or not 1 <= args.jobs <= 6):
    parser.error("require size 512–16384, frames >= 4, seconds >= 10, warm-up 0–100000, jobs 1–6")
if args.binary is None and not args.html_only:
    parser.error("a renderer binary is required")
binary = args.binary.resolve() if args.binary else None


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def write_png(target):
    header, dimensions, maximum, pixels = target.with_suffix(".ppm").read_bytes().split(b"\n", 3)
    if (header != b"P6" or dimensions != f"{args.size} {args.size}".encode()
            or maximum != b"255" or len(pixels) != args.size**2 * 3):
        raise ValueError("Unexpected renderer PPM output: " + str(target))
    stride = args.size * 3
    rows = b"".join(b"\0" + pixels[y * stride:(y + 1) * stride] for y in range(args.size))
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", args.size, args.size, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")
    target.with_suffix(".png").write_bytes(png)


cpu = "unknown"
if Path("/proc/cpuinfo").exists():
    for line in Path("/proc/cpuinfo").read_text().splitlines():
        if line.startswith("model name"):
            cpu = line.split(":", 1)[1].strip()
            break
try:
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root,
                                       text=True, stderr=subprocess.DEVNULL).strip()
except (subprocess.CalledProcessError, FileNotFoundError):
    revision = "unknown"
compile_command = None
commands = binary.parent.parent / "compile_commands.json" if binary else None
if commands and commands.exists():
    for entry in json.loads(commands.read_text()):
        if entry["file"].endswith("/upt/upt.c"):
            compile_command = entry.get("command", entry.get("arguments"))
            break
report = {
    "utc": datetime.now(timezone.utc).isoformat(), "cpu": cpu,
    "binary": str(binary), "compile_command": compile_command,
    "size": args.size, "frames": args.frames,
    "min_seconds": args.min_seconds,
    "warmup": args.warmup, "samples_per_pixel_per_frame": 8, "threads": 1,
    "revision": revision,
    "scenes": [],
}
metrics = root / ".cache/renders/metrics.json"
if args.html_only:
    report = json.loads(metrics.read_text())
elif args.resume and metrics.exists():
    previous = json.loads(metrics.read_text())
    for key in ("binary", "compile_command", "size", "frames", "min_seconds", "warmup", "revision"):
        if previous.get(key) != report[key]:
            parser.error("cannot resume: different " + key)
    report = previous
scenes = [
    ("original", "Original", "original", "../upt/scenes/original.c", ""),
    ("atelier", "Atelier", "atelier", "../upt/scenes/atelier.c", ""),
    ("glass", "Glass Buddha — front view", "scenes/glass.pbrt", "glass.pbrt", "refined"),
    ("dragon", "Dragon — north view", "scenes/dragon.pbrt", "dragon.pbrt", "refined"),
    ("bunnies", "Porcelain gallery", "scenes/bunnies.pbrt", "bunnies.pbrt", "refined"),
    ("underwater", "Underwater fish and terrain", "scenes/underwater.pbrt", "underwater.pbrt", "refined"),
]


def render(entry):
    name, title, scene, source, directory = entry
    target = root / ".cache/renders" / directory / name
    target.parent.mkdir(parents=True, exist_ok=True)
    frames = args.frames
    while True:
        command = [str(binary), "--warmup", str(args.warmup), str(frames), scene,
                   str(args.size), str(args.size), str(target.with_suffix(".ppm"))]
        print(f"Rendering {title}: {frames} frames at {args.size} × {args.size}", flush=True)
        output = subprocess.check_output(command, cwd=root, text=True)
        print(title + ": " + output.strip(), flush=True)
        match = re.search(r"Rendered (\d+) frames in ([\d.]+) seconds .*checksum ([0-9a-f]+)", output)
        if not match or int(match[1]) != frames or float(match[2]) <= 0:
            raise ValueError("Unexpected timing output: " + output)
        seconds = float(match[2])
        if seconds >= args.min_seconds:
            break
        frames = max(frames + 1, math.ceil(frames * args.min_seconds * 1.1 / seconds))
    write_png(target)
    return {
        "name": name, "title": title, "source": source,
        "frames": frames,
        "image": "../" + str(target.relative_to(root)), "seconds": seconds,
        "fps": frames / seconds, "ms_per_frame": 1000 * seconds / frames,
        "primary_samples_per_second": args.size**2 * 8 * frames / seconds,
        "checksum": match[3], "concurrent_renderers": args.jobs,
    }


if not args.html_only:
    done = {scene["name"]: scene for scene in report["scenes"]
            if scene.get("frames", 0) >= args.frames and scene["seconds"] >= args.min_seconds}
    for scene in done.values():
        for extension in (".png", ".ppm"):
            if not (root / "scenes" / (scene["image"] + extension)).exists():
                parser.error("cannot resume: missing image for " + scene["name"])
    with ThreadPoolExecutor(max_workers=args.jobs) as workers:
        pending = [workers.submit(render, scene) for scene in scenes if scene[0] not in done]
        for future in as_completed(pending):
            scene = future.result()
            done[scene["name"]] = scene
            report["scenes"] = [done[entry[0]] for entry in scenes if entry[0] in done]
            metrics.write_text(json.dumps(report, indent=2) + "\n")

concurrency = max((scene.get("concurrent_renderers", 1) for scene in report["scenes"]), default=1)
page = ['<!doctype html><html lang="en"><meta charset="utf-8">',
        '<meta name="viewport" content="width=device-width, initial-scale=1">',
        '<title>UPT benchmarks</title>',
        '<style>body{font:16px/1.5 sans-serif;max-width:700px;margin:24px auto;padding:0 16px}'
        'img{display:block;width:512px;max-width:100%;height:auto}section{margin:32px 0}'
        'code{overflow-wrap:anywhere}small{color:#555}</style>',
        '<h1>UPT benchmarks</h1>',
        f'<p>{report["size"]} × {report["size"]}; at least {report["min_seconds"]:g} measured seconds '
        f'per scene after {report["warmup"]} warm-up frames.</p>',
        f'<p>CPU: {html.escape(report["cpu"])}. One rendering thread per process.</p>',
        f'<p>Up to {concurrency} concurrent renderers. Timings are approximate under shared machine load. '
        'Use sequential runs on an idle machine for performance comparisons.</p>',
        '<p><small>Timing includes tracing, accumulation and display conversion. Loading, BVH/light '
        'construction, warm-up, image writing and cleanup are excluded. Samples/s counts primary samples.</small></p>']
for scene in report["scenes"]:
    image = html.escape(scene["image"])
    title = html.escape(scene["title"])
    page += [f'<section><h2>{title}</h2>',
             f'<a href="{image}.png"><img src="{image}.png" width="512" height="512" alt="{title}"></a>',
             f'<p>{scene["frames"]} measured frames; '
             f'{(scene["frames"] + report["warmup"]) * report["samples_per_pixel_per_frame"]} samples/pixel in image.</p>',
             f'<p>{scene["seconds"]:.3f} s total · {scene["fps"]:.3f} FPS · '
             f'{scene["ms_per_frame"]:.1f} ms/frame · {scene["primary_samples_per_second"] / 1e6:.3f} M samples/s</p>',
             f'<p><a href="{image}.png">PNG</a> · <a href="{image}.ppm" download>PPM</a> · '
             f'<a href="{html.escape(scene["source"])}">Scene/source</a></p></section><hr>']
page += ['<p><a href="../.cache/renders/metrics.json">Full metrics</a></p>',
         '</html>']
(root / "scenes/gallery.html").write_text("\n".join(page) + "\n")
print((root / "scenes/gallery.html").as_uri())
