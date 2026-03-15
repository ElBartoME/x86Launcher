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
    tmpdir    = tempfile.mkdtemp(prefix="fli_")
    frames    = os.path.join(tmpdir, "frames")
    palette   = os.path.join(tmpdir, "palette.png")
    trimmed   = os.path.join(tmpdir, "trimmed.mp4")

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

        # Step 2: Generate a 208-colour palette from the trimmed clip
        print("\n=== Step 2/4: Building 208-colour palette ===")
        run([
            "ffmpeg", "-y",
            "-i", trimmed,
            "-vf", f"fps={fps},scale=320:200:flags=lanczos,"
                   "palettegen=max_colors=208:reserve_transparent=0:stats_mode=full",
            palette
        ], "Generate palette")

        # Step 3: Extract quantized PNG frames
        print("\n=== Step 3/4: Extracting frames ===")
        run([
            "ffmpeg", "-y",
            "-i", trimmed,
            "-i", palette,
            "-filter_complex",
            f"fps={fps},scale=320:200:flags=lanczos[v];[v][1:v]paletteuse=dither=bayer:bayer_scale=2",
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
