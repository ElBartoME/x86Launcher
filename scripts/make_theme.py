"""
make_theme.py - Full pipeline: audio file -> 8-bit PCM WAV for x86Launcher.

Usage:
    python make_theme.py <input_audio> <output.wav> <start_time> <duration> [fade_in_ms] [fade_out_ms]

Examples:
    python make_theme.py theme.mp3 theme.wav 00:00:10 00:00:25
    python make_theme.py theme.mp3 theme.wav 00:00:10 00:00:25 500 2000
    python make_theme.py soundtrack.ogg theme.wav 00:01:30 00:00:20 0 1500

Parameters:
    input_audio  - any audio file ffmpeg can read (mp3, ogg, flac, wav, etc.)
    output.wav   - output WAV file (8-bit unsigned PCM, 22050Hz mono)
    start_time   - start offset in HH:MM:SS or seconds
    duration     - clip duration in HH:MM:SS or seconds
    fade_in_ms   - fade in duration in milliseconds (default: 500)
    fade_out_ms  - fade out duration in milliseconds (default: 1500)

Requirements:
    - ffmpeg in PATH
    - fade_wav.py in the same directory as this script
"""

import sys
import os
import subprocess
import tempfile

def run(cmd, desc):
    print(f"\n[{desc}]")
    print("  " + " ".join(cmd))
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"ERROR: {desc} failed!")
        print(result.stderr)
        sys.exit(1)
    return result

def make_theme(input_audio, output_wav, start_time, duration,
               fade_in_ms=500, fade_out_ms=1500):

    script_dir = os.path.dirname(os.path.abspath(__file__))
    fade_wav   = os.path.join(script_dir, "fade_wav.py")

    if not os.path.exists(fade_wav):
        print(f"ERROR: fade_wav.py not found at {fade_wav}")
        sys.exit(1)

    if not os.path.exists(input_audio):
        print(f"ERROR: Input file not found: {input_audio}")
        sys.exit(1)

    tmpfile = tempfile.mktemp(suffix="_raw.wav")

    try:
        # Step 1: Extract, trim, normalize and convert to 8-bit 22050Hz mono
        print(f"\n=== Step 1/2: Extracting and converting audio ===")
        print(f"  Source : {input_audio}")
        print(f"  Start  : {start_time}")
        print(f"  Duration: {duration}")
        run([
            "ffmpeg", "-y",
            "-ss", start_time,
            "-i", input_audio,
            "-t", duration,
            "-ar", "22050",
            "-ac", "1",
            "-acodec", "pcm_u8",
            tmpfile
        ], "Extract and convert audio")

        size_raw = os.path.getsize(tmpfile)
        print(f"  Raw WAV : {size_raw // 1024} KB")

        # Step 2: Apply fade in/out
        print(f"\n=== Step 2/2: Applying fades ===")
        print(f"  Fade in : {fade_in_ms}ms")
        print(f"  Fade out: {fade_out_ms}ms")
        result = subprocess.run(
            ["python", fade_wav, tmpfile, output_wav,
             str(fade_in_ms), str(fade_out_ms)],
            capture_output=False
        )
        if result.returncode != 0:
            print("ERROR: fade_wav.py failed")
            sys.exit(1)

        print(f"\n=== Done! ===")
        print(f"Output: {os.path.abspath(output_wav)}")
        size = os.path.getsize(output_wav)
        print(f"Size  : {size // 1024} KB ({size / 22050:.1f}s at 22050Hz mono 8-bit)")

    finally:
        if os.path.exists(tmpfile):
            os.remove(tmpfile)

if __name__ == "__main__":
    if len(sys.argv) < 5:
        print(__doc__)
        sys.exit(1)

    input_audio = sys.argv[1]
    output_wav  = sys.argv[2]
    start_time  = sys.argv[3]
    duration    = sys.argv[4]
    fade_in_ms  = int(sys.argv[5]) if len(sys.argv) > 5 else 500
    fade_out_ms = int(sys.argv[6]) if len(sys.argv) > 6 else 1500

    make_theme(input_audio, output_wav, start_time, duration,
               fade_in_ms, fade_out_ms)
