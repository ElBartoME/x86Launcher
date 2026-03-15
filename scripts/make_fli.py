"""
make_fli.py - Convert a folder of PNG frames into a FLI file for x86Launcher.

Usage:
    python make_fli.py <frames_folder> <output.fli> [fps]

Example:
    python make_fli.py frames preview.fli 15

Requirements:
    pip install pillow

Notes:
    - Frames must be named frame0001.png, frame0002.png etc (ffmpeg default)
    - Output is a FLI file (0xAF11 magic), 320x200, max 208 colours
    - Palette entry 0 is never written by the FLI player (reserved for UI).
      All pixels that the quantizer assigns to entry 0 are remapped to
      entry 1 instead. Entry 1 is forced to the same colour as entry 0
      in the palette so the remap is visually transparent.
    - Entries 208-255 are left black so the UI palette is never disturbed.
"""

import sys
import os
import glob
import struct
from PIL import Image

FLI_MAX_COLOURS = 208   # entries 0-207 available for video

# FLI format constants
FLI_MAGIC          = 0xAF11
FLI_FRAME_MAGIC    = 0xF1FA
FLI_CHUNK_COLOR_64 = 11
FLI_CHUNK_BYTE_RUN = 15

# -------------------------------------------------------
# Frame loading and palette building
# -------------------------------------------------------

def load_frames(folder):
    pattern = os.path.join(folder, "frame*.png")
    paths = sorted(glob.glob(pattern))
    if not paths:
        print(f"ERROR: No frame*.png files found in {folder}")
        sys.exit(1)
    print(f"Found {len(paths)} frames in {folder}")
    return paths

def build_global_palette(paths):
    """
    Sample frames, composite into a grid, quantize to FLI_MAX_COLOURS.
    Then force entry 1 to the same colour as entry 0 so that remapping
    all entry-0 pixels to entry-1 is visually transparent.
    """
    print("Building global palette from all frames...")
    step = max(1, len(paths) // 64)
    sample_frames = []
    for i, p in enumerate(paths):
        if i % step == 0:
            sample_frames.append(Image.open(p).convert("RGB"))

    w, h = sample_frames[0].size
    cols = 8
    rows = (len(sample_frames) + cols - 1) // cols
    grid = Image.new("RGB", (w * cols, h * rows))
    for idx, frame in enumerate(sample_frames):
        gx = (idx % cols) * w
        gy = (idx // cols) * h
        grid.paste(frame, (gx, gy))

    palette_img = grid.quantize(colors=FLI_MAX_COLOURS, dither=0)

    # Force entry 1 to the same colour as entry 0.
    # This means any pixel remapped from 0->1 looks identical.
    pal = palette_img.getpalette()
    pal[3] = pal[0]   # entry 1 R = entry 0 R
    pal[4] = pal[1]   # entry 1 G = entry 0 G
    pal[5] = pal[2]   # entry 1 B = entry 0 B
    palette_img.putpalette(pal)

    print(f"Palette built from {len(sample_frames)} sample frames")
    print(f"Entry 0 colour: RGB({pal[0]}, {pal[1]}, {pal[2]}) "
          f"-> also assigned to entry 1")
    return palette_img

# -------------------------------------------------------
# FLI chunk encoders
# -------------------------------------------------------

def encode_palette_chunk(palette_img):
    """
    Build a COLOR_64 chunk (type 11).
    FLI 0xAF11 uses 6-bit (0-63) RGB values.
    Writes all 256 entries:
      - Entry 0: written normally (but the FLI player skips applying it,
                 so the UI colour is preserved at runtime)
      - Entries 1-207: from the quantized palette
      - Entries 208-255: forced black (UI reserved range)
    """
    raw = palette_img.getpalette()
    
    # Pad to 256 entries (768 values) in case Pillow returns fewer
    if raw is not None:
        raw = list(raw) + [0] * (768 - len(raw))
    

    data = bytearray()
    data += struct.pack("<H", 1)   # num_packets = 1
    data += struct.pack("<B", 0)   # skip_count  = 0
    data += struct.pack("<B", 0)   # change_count = 0 means all 256

    for i in range(256):
        if i < FLI_MAX_COLOURS and raw is not None:
            r = raw[i * 3]     >> 2
            g = raw[i * 3 + 1] >> 2
            b = raw[i * 3 + 2] >> 2
        else:
            r = g = b = 0
        data += struct.pack("<BBB", r, g, b)

    chunk_size = 6 + len(data)
    return struct.pack("<IH", chunk_size, FLI_CHUNK_COLOR_64) + bytes(data)

def encode_byte_run_chunk(pixels, width, height):
    """
    Encode a full frame as BYTE_RUN (type 15).
    Each row is RLE-encoded independently.
    """
    data = bytearray()

    for row in range(height):
        row_pixels = pixels[row * width:(row + 1) * width]
        row_data   = bytearray()
        col        = 0

        while col < width:
            run_val = row_pixels[col]
            run_len = 1
            while (col + run_len < width and
                   run_len < 127 and
                   row_pixels[col + run_len] == run_val):
                run_len += 1

            if run_len > 2:
                row_data += struct.pack("<bB", run_len, run_val)
                col += run_len
            else:
                lit_len = 0
                while col + lit_len < width and lit_len < 127:
                    if (col + lit_len + 2 < width and
                            row_pixels[col + lit_len]     == row_pixels[col + lit_len + 1] and
                            row_pixels[col + lit_len + 1] == row_pixels[col + lit_len + 2]):
                        break
                    lit_len += 1
                if lit_len == 0:
                    lit_len = 1
                row_data += struct.pack("<b", -lit_len)
                row_data += bytes(row_pixels[col:col + lit_len])
                col += lit_len

        data += struct.pack("<B", 0) + bytes(row_data)

    chunk_size = 6 + len(data)
    return struct.pack("<IH", chunk_size, FLI_CHUNK_BYTE_RUN) + bytes(data)

def encode_frame(pixels, width, height, palette_chunk=None):
    chunks     = b""
    num_chunks = 0

    if palette_chunk is not None:
        chunks     += palette_chunk
        num_chunks += 1

    chunks     += encode_byte_run_chunk(pixels, width, height)
    num_chunks += 1

    frame_size = 16 + len(chunks)
    header = struct.pack("<IHH8s",
        frame_size,
        FLI_FRAME_MAGIC,
        num_chunks,
        b'\x00' * 8)
    return header + chunks

# -------------------------------------------------------
# Top-level writer
# -------------------------------------------------------

def make_fli(paths, output_path, fps=15, palette_img=None):

    if palette_img is None:
        palette_img = build_global_palette(paths)

    width  = 320
    height = 200

    speed = max(1, round(70 / fps))

    print(f"Quantizing {len(paths)} frames...")
    quantized = []
    remap_count = 0

    for i, p in enumerate(paths):
        img = Image.open(p).convert("RGB")
        if img.size != (width, height):
            img = img.resize((width, height), Image.LANCZOS)

        q = img.quantize(colors=FLI_MAX_COLOURS, palette=palette_img, dither=1)

        # Remap any pixel assigned to entry 0 -> entry 1.
        # Entry 1 has been forced to the same colour as entry 0 in the
        # palette, so this is visually transparent.
        pixels = bytearray(q.tobytes())
        for j in range(len(pixels)):
            if pixels[j] == 0:
                pixels[j] = 1
                remap_count += 1

        quantized.append(bytes(pixels))

        if (i + 1) % 30 == 0:
            print(f"  {i+1}/{len(paths)} frames quantized")

    print(f"  Total entry-0 pixels remapped to entry-1: {remap_count}")

    print(f"Writing {output_path}...")

    pal_chunk  = encode_palette_chunk(palette_img)
    frame_data = b""
    for i, pixels in enumerate(quantized):
        frame_data += encode_frame(pixels, width, height,
                                   palette_chunk=pal_chunk if i == 0 else None)

    num_frames = len(quantized)
    file_size  = 128 + len(frame_data)

    header = struct.pack("<IHHHHHHH",
        file_size,
        FLI_MAGIC,
        num_frames,
        width,
        height,
        8,
        0,
        speed,
    )
    header += b'\x00' * (128 - len(header))

    with open(output_path, "wb") as f:
        f.write(header)
        f.write(frame_data)

    size = os.path.getsize(output_path)
    print(f"Done. {output_path} written ({size // 1024} KB, "
          f"{num_frames} frames at {fps}fps, "
          f"{num_frames / fps:.1f}s)")

    with open(output_path, "rb") as f:
        f.read(4)
        magic = struct.unpack("<H", f.read(2))[0]
    if magic == 0xAF11:
        print("Magic: 0xAF11 (FLI) - OK")
    elif magic == 0xAF12:
        print("Magic: 0xAF12 (FLC) - OK")
    else:
        print(f"WARNING: Unexpected magic 0x{magic:04X}")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)

    frames_folder = sys.argv[1]
    output_file   = sys.argv[2]
    fps           = int(sys.argv[3]) if len(sys.argv) > 3 else 15

    paths = load_frames(frames_folder)
    make_fli(paths, output_file, fps=fps)