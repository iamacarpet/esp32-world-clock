#!/usr/bin/env python3
"""Compile & run the headless ESPHome host emulators and post-process screenshots & videos."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


REPO_ROOT = Path(__file__).resolve().parent.parent


def run_cmd(cmd, env=None, cwd=None, timeout=600):
    prefix = ["nice", "-n", "10"] if shutil.which("nice") else []
    full_cmd = prefix + cmd
    print(f"+ {' '.join(full_cmd)}", flush=True)
    subprocess.run(full_cmd, env=env, cwd=cwd, check=True, timeout=timeout)


def find_esphome_cmd():
    if shutil.which("esphome"):
        return ["esphome"]
    if shutil.which("uvx"):
        return ["uvx", "--from", "esphome==2026.10.2", "esphome"]
    raise RuntimeError("Neither 'esphome' nor 'uvx' found in PATH")


def convert_bmp_to_png(bmp_path: Path, png_path: Path):
    if shutil.which("convert"):
        run_cmd(["convert", str(bmp_path), str(png_path)])
    else:
        from PIL import Image  # type: ignore

        with Image.open(bmp_path) as im:
            im.save(png_path, format="PNG", optimize=True)


def build_contact_sheet(png_files: list[Path], out_path: Path):
    if not png_files:
        return
    if shutil.which("montage"):
        cmd = [
            "montage",
            *[str(p) for p in png_files],
            "-tile",
            "4x",
            "-geometry",
            "400x240+6+6",
            "-background",
            "#0b0c10",
            "-fill",
            "#e5e7eb",
            "-pointsize",
            "13",
            str(out_path),
        ]
        run_cmd(cmd)
    elif shutil.which("convert"):
        # Fallback: simple 4-column grid using ImageMagick convert
        rows = []
        tmp_rows = []
        for idx in range(0, len(png_files), 4):
            chunk = png_files[idx : idx + 4]
            row_img = out_path.parent / f"_row_{idx}.png"
            cmd = ["convert"]
            for p in chunk:
                cmd += ["(", str(p), "-resize", "400x240", "-bordercolor", "#1f2937", "-border", "3", ")"]
            cmd += ["+append", str(row_img)]
            run_cmd(cmd)
            tmp_rows.append(row_img)
        run_cmd(["convert", *[str(r) for r in tmp_rows], "-background", "#0b0c10", "-append", str(out_path)])
        for r in tmp_rows:
            r.unlink(missing_ok=True)


def encode_mp4_from_gifs(gif_files: list[Path], out_dir: Path, keep_gifs: bool):
    if not shutil.which("ffmpeg") or not gif_files:
        return
    mp4_files = []
    for gif in gif_files:
        mp4 = out_dir / f"{gif.stem}.mp4"
        run_cmd(
            [
                "ffmpeg",
                "-y",
                "-i",
                str(gif),
                "-movflags",
                "faststart",
                "-pix_fmt",
                "yuv420p",
                "-vf",
                "scale=800:480:flags=neighbor",
                "-c:v",
                "libx264",
                "-crf",
                "22",
                str(mp4),
            ]
        )
        mp4_files.append(mp4)
        if not keep_gifs:
            gif.unlink(missing_ok=True)

    if len(mp4_files) > 1:
        concat_list = out_dir / "_concat.txt"
        concat_list.write_text("\n".join(f"file '{p.name}'" for p in mp4_files) + "\n")
        demo_reel = out_dir / "demo_reel.mp4"
        run_cmd(
            [
                "ffmpeg",
                "-y",
                "-f",
                "concat",
                "-safe",
                "0",
                "-i",
                str(concat_list),
                "-c",
                "copy",
                str(demo_reel),
            ]
        )
        concat_list.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description="Run headless ESPHome World Clock emulators")
    parser.add_argument("--out", default=str(REPO_ROOT / "sim_output"), help="Output directory for PNGs/GIFs/MP4s")
    parser.add_argument(
        "--video-seconds",
        type=int,
        default=30,
        help="Duration in seconds for each animated mode video (3..60, default: 30)",
    )
    parser.add_argument(
        "--keep-gifs",
        action="store_true",
        help="Keep raw GIF files even after encoding H.264 MP4 videos with ffmpeg",
    )
    parser.add_argument(
        "--sims",
        nargs="+",
        default=["sim/leo-clock-sim.yaml", "sim/office-clock-sim.yaml"],
        help="Simulator YAML files to compile and run",
    )
    args = parser.parse_args()

    video_secs = max(3, min(60, args.video_seconds))
    out_dir = Path(args.out).resolve()
    if out_dir.exists():
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    esphome_cmd = find_esphome_cmd()
    env = os.environ.copy()
    env["ESPHOME_SNAPSHOT_DIR"] = str(out_dir)
    env["SIM_VIDEO_SECONDS"] = str(video_secs)

    # Allow generous timeout for 11 animated videos (up to 60s each + stabilisation)
    run_timeout = max(300, video_secs * 15 + 180)

    for sim_rel in args.sims:
        sim_yaml = (REPO_ROOT / sim_rel).resolve()
        print(f"\n=== Building & running {sim_rel} (video_seconds={video_secs}s) ===", flush=True)
        run_cmd([*esphome_cmd, "compile", str(sim_yaml)], env=env, cwd=REPO_ROOT)

        dev_name = sim_yaml.stem
        bin_path = sim_yaml.parent / ".esphome" / "build" / dev_name / ".pioenvs" / dev_name / "program"
        if not bin_path.exists():
            matches = list((sim_yaml.parent / ".esphome" / "build").glob("**/program"))
            if not matches:
                raise FileNotFoundError(f"Could not locate compiled host binary for {sim_rel}")
            bin_path = matches[0]
        run_cmd([str(bin_path)], env=env, cwd=REPO_ROOT, timeout=run_timeout)

    # Convert BMP snapshots -> PNG
    bmp_files = sorted(out_dir.glob("*.bmp"))
    png_files = []
    for bmp in bmp_files:
        png = bmp.with_suffix(".png")
        convert_bmp_to_png(bmp, png)
        bmp.unlink()
        png_files.append(png)

    build_contact_sheet(png_files, out_dir / "overview_gallery.png")

    gif_files = sorted(out_dir.glob("*.gif"))
    encode_mp4_from_gifs(gif_files, out_dir, keep_gifs=args.keep_gifs)

    print(f"\n=== Capture complete! Artifacts in {out_dir} ===")
    for p in sorted(out_dir.iterdir()):
        print(f"  {p.name:40s} ({p.stat().st_size / 1024:.1f} KB)")


if __name__ == "__main__":
    main()
