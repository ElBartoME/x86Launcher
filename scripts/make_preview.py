"""
make_preview.py - Full pipeline: video file -> FLI preview for x86Launcher.

Usage:
    python make_preview.py <input_video> <output.fli> <start_time> <duration> [fps]

Examples:
    python make_preview.py gameplay.mp4 preview.fli 00:02:30 00:00:12
    python make_preview.py gameplay.mp4 preview.fli 00:02:30 00:00:12 15

Parameters:
    input_video  - any video file ffmpeg can read (mp4, avi, mkv, etc.)
    output.fli   - output FLI file
    start_time   - start offset in HH:MM:SS or seconds
    duration     - clip duration in HH:MM:SS or seconds
    fps          - frames per second (default: 15)

Requirements:
    - ffmpeg in PATH
    - pip install pillow
    - make_fli.py in the same directory as this script
"""

import sys
import os
import subprocess
import tempfile
import shutil
import glob


def run(cmd, desc):
    print(f"\n[{desc}]")
    print("  " + " ".join(cmd))
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"ERROR: {desc} failed!")
        print(result.stderr)
        sys.exit(1)
    return result


def detect_crop(video_path):
    """
    Use ffmpeg cropdetect to find black border crop parameters.
    Samples 10 seconds starting 5 seconds in to avoid fades/logos.
    Returns a crop string like "1280:720:0:0", or None if no crop needed.
    """
    print("\n=== Step 1b: Detecting black borders ===")
    result = subprocess.run([
        "ffmpeg", "-y",
        "-ss", "5",
        "-t", "10",
        "-i", video_path,
        "-vf", "cropdetect=limit=16:round=2:reset=0",
        "-f", "null", "-"
    ], capture_output=True, text=True)

    crop = None
    for line in result.stderr.splitlines():
        if "crop=" in line:
            crop = line.split("crop=")[-1].strip()

    if crop:
        # Sanity-check: if the crop matches the full frame, skip it
        # e.g. "1920:1080:0:0" on a 1920x1080 video means no borders
        parts = crop.split(":")
        if len(parts) == 4 and parts[2] == "0" and parts[3] == "0":
            # Check if w/h match source resolution by probing
            probe = subprocess.run([
                "ffprobe", "-v", "error",
                "-select_streams", "v:0",
                "-show_entries", "stream=width,height",
                "-of", "csv=p=0",
                video_path
            ], capture_output=True, text=True)
            probe_out = probe.stdout.strip()
            if probe_out:
                src_w, src_h = probe_out.split(",")
                if parts[0] == src_w and parts[1] == src_h:
                    print("  No black borders detected.")
                    return None

        print(f"  Crop region: {crop}")
    else:
        print("  No black borders detected.")

    return crop


def make_preview(input_video, output_fli, start_time, duration, fps=15):

    script_dir = os.path.dirname(os.path.abspath(__file__))
    make_fli   = os.path.join(script_dir, "make_fli.py")

    if not os.path.exists(make_fli):
        print(f"ERROR: make_fli.py not found at {make_fli}")
        sys.exit(1)

    if not os.path.exists(input_video):
        print(f"ERROR: Input file not found: {input_video}")
        sys.exit(1)

    # Work in a temp directory so frames don't clutter the working dir
    tmpdir  = tempfile.mkdtemp(prefix="fli_")
    frames  = os.path.join(tmpdir, "frames")
    palette = os.path.join(tmpdir, "palette.png")
    trimmed = os.path.join(tmpdir, "trimmed.mp4")

    os.makedirs(frames)

    try:
        # Step 1: Trim the clip (re-encode for clean start frame)
        print(f"\n=== Step 1/4: Trimming {duration} starting at {start_time} ===")
        run([
            "ffmpeg", "-y",
            "-ss", start_time,
            "-i", input_video,
            "-t", duration,
            trimmed
        ], "Trim clip")

        # Step 1b: Auto-detect and remove black borders
        crop      = detect_crop(trimmed)
        crop_filter = f"crop={crop}," if crop else ""

        # Step 2: Generate a 208-colour palette from the trimmed clip.
        # stats_mode=diff prioritises colours that change between frames,
        # which gives a more stable palette and reduces inter-frame noise.
        print("\n=== Step 2/4: Building 208-colour palette ===")
        run([
            "ffmpeg", "-y",
            "-i", trimmed,
            "-vf", (
                f"fps={fps},"
                f"{crop_filter}"
                f"scale=320:200:flags=lanczos,"
                f"palettegen=max_colors=208:reserve_transparent=0:stats_mode=diff"
            ),
            palette
        ], "Generate palette")

        # Step 3: Extract quantized PNG frames.
        # dither=none avoids inter-frame dancing noise; works well for pixel
        # art sources where the original palette is already carefully chosen.
        print("\n=== Step 3/4: Extracting frames ===")
        run([
            "ffmpeg", "-y",
            "-i", trimmed,
            "-i", palette,
            "-filter_complex",
            (
                f"fps={fps},"
                f"{crop_filter}"
                f"scale=320:200:flags=lanczos"
                f"[v];[v][1:v]paletteuse=dither=none"
            ),
            os.path.join(frames, "frame%04d.png").replace("\\", "/")
        ], "Extract frames")

        found = len(glob.glob(os.path.join(frames, "frame*.png")))
        print(f"\n  {found} frames extracted")

        if found == 0:
            print("ERROR: No frames were extracted")
            sys.exit(1)

        # Step 4: Convert frames to FLI
        print(f"\n=== Step 4/4: Converting to FLI ({fps}fps) ===")
        result = subprocess.run(
            ["python", make_fli, frames, output_fli, str(fps)],
            capture_output=False
        )
        if result.returncode != 0:
            print("ERROR: make_fli.py failed")
            sys.exit(1)

        print(f"\n=== Done! ===")
        print(f"Output: {os.path.abspath(output_fli)}")
        size = os.path.getsize(output_fli)
        print(f"Size  : {size // 1024} KB")
        print(f"Frames: {found} at {fps}fps = {found/fps:.1f}s")

    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


if __name__ == "__main__":
    if len(sys.argv) < 5:
        print(__doc__)
        sys.exit(1)

    input_video = sys.argv[1]
    output_fli  = sys.argv[2]
    start_time  = sys.argv[3]
    duration    = sys.argv[4]
    fps         = int(sys.argv[5]) if len(sys.argv) > 5 else 15

    make_preview(input_video, output_fli, start_time, duration, fps)