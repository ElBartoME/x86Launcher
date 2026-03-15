"""
fade_wav.py - Add fade in and fade out to an 8-bit or 16-bit PCM WAV file.

Usage:
    python fade_wav.py <input.wav> <output.wav> [fade_in_ms] [fade_out_ms]

Example:
    python fade_wav.py theme_raw.wav theme.wav 500 1500

Defaults:
    fade_in_ms  = 500  ms
    fade_out_ms = 1500 ms

Supports:
    - 8-bit unsigned PCM  (silence = 128)
    - 16-bit signed PCM   (silence = 0), mono or stereo
"""

import sys
import struct
import os

def read16(data, offset):
    return struct.unpack_from('<H', data, offset)[0]

def read32(data, offset):
    return struct.unpack_from('<I', data, offset)[0]

def write16(val):
    return struct.pack('<H', val)

def write32(val):
    return struct.pack('<I', val)

def find_chunk(data, tag):
    """Search for a RIFF chunk by 4-byte tag. Returns (data_offset, size)."""
    i = 12
    while i + 8 <= len(data):
        chunk_tag  = data[i:i+4]
        chunk_size = read32(data, i + 4)
        if chunk_tag == tag:
            return i + 8, chunk_size
        i += 8 + chunk_size
        if chunk_size % 2 != 0:
            i += 1
    return None, None

def fade_wav(input_path, output_path, fade_in_ms=500, fade_out_ms=1500):
    with open(input_path, 'rb') as f:
        raw = bytearray(f.read())

    print(f"Input: {input_path} ({len(raw)} bytes)")

    if raw[0:4] != b'RIFF' or raw[8:12] != b'WAVE':
        print("ERROR: Not a RIFF/WAVE file")
        sys.exit(1)

    fmt_offset, fmt_size = find_chunk(raw, b'fmt ')
    if fmt_offset is None:
        print("ERROR: No fmt chunk found")
        sys.exit(1)

    fmt_tag     = read16(raw, fmt_offset)
    channels    = read16(raw, fmt_offset + 2)
    sample_rate = read32(raw, fmt_offset + 4)
    bits        = read16(raw, fmt_offset + 14)

    print(f"Format      : {'PCM' if fmt_tag == 1 else fmt_tag}")
    print(f"Channels    : {channels}")
    print(f"Sample rate : {sample_rate} Hz")
    print(f"Bit depth   : {bits}-bit")

    if fmt_tag != 1:
        print(f"ERROR: Not PCM (fmt_tag={fmt_tag})")
        sys.exit(1)

    if bits not in (8, 16):
        print(f"ERROR: Only 8-bit and 16-bit PCM supported (got {bits}-bit)")
        sys.exit(1)

    data_offset, data_size = find_chunk(raw, b'data')
    if data_offset is None:
        print("ERROR: No data chunk found")
        sys.exit(1)

    print(f"PCM data    : {data_size} bytes at offset {data_offset}")

    bytes_per_sample = bits // 8
    bytes_per_frame  = channels * bytes_per_sample
    total_frames     = data_size // bytes_per_frame

    print(f"Total frames: {total_frames} ({total_frames / sample_rate:.2f}s)")

    fade_in_frames  = int(sample_rate * fade_in_ms  / 1000)
    fade_out_frames = int(sample_rate * fade_out_ms / 1000)

    if fade_in_frames + fade_out_frames > total_frames:
        half = total_frames // 2
        fade_in_frames  = half
        fade_out_frames = half
        print(f"WARNING: Fades overlap - clamped to {half} frames each")

    print(f"Fade in     : {fade_in_frames} frames ({fade_in_ms}ms)")
    print(f"Fade out    : {fade_out_frames} frames ({fade_out_ms}ms)")

    pcm    = bytearray(raw[data_offset:data_offset + data_size])
    result = bytearray(pcm)

    if bits == 8:
        # 8-bit unsigned PCM: silence = 128, range 0-255
        for i in range(fade_in_frames):
            scale = i / fade_in_frames
            for ch in range(channels):
                idx    = i * bytes_per_frame + ch
                sample = pcm[idx] - 128
                sample = int(sample * scale)
                sample = max(-128, min(127, sample))
                result[idx] = sample + 128

        fade_out_start = total_frames - fade_out_frames
        for i in range(fade_out_frames):
            scale = 1.0 - (i / fade_out_frames)
            for ch in range(channels):
                idx    = (fade_out_start + i) * bytes_per_frame + ch
                sample = pcm[idx] - 128
                sample = int(sample * scale)
                sample = max(-128, min(127, sample))
                result[idx] = sample + 128

    elif bits == 16:
        # 16-bit signed PCM: silence = 0, range -32768 to 32767
        for i in range(fade_in_frames):
            scale = i / fade_in_frames
            for ch in range(channels):
                idx    = i * bytes_per_frame + ch * 2
                sample = struct.unpack_from('<h', pcm, idx)[0]
                sample = int(sample * scale)
                sample = max(-32768, min(32767, sample))
                struct.pack_into('<h', result, idx, sample)

        fade_out_start = total_frames - fade_out_frames
        for i in range(fade_out_frames):
            scale = 1.0 - (i / fade_out_frames)
            for ch in range(channels):
                idx    = (fade_out_start + i) * bytes_per_frame + ch * 2
                sample = struct.unpack_from('<h', pcm, idx)[0]
                sample = int(sample * scale)
                sample = max(-32768, min(32767, sample))
                struct.pack_into('<h', result, idx, sample)

    # Write canonical WAV header + faded PCM
    out = bytearray()
    out += b'RIFF'
    out += write32(36 + data_size)
    out += b'WAVE'
    out += b'fmt '
    out += write32(16)
    out += write16(1)                                    # PCM
    out += write16(channels)
    out += write32(sample_rate)
    out += write32(sample_rate * channels * bytes_per_sample)  # byte rate
    out += write16(channels * bytes_per_sample)          # block align
    out += write16(bits)
    out += b'data'
    out += write32(data_size)
    out += result

    with open(output_path, 'wb') as f:
        f.write(out)

    size = os.path.getsize(output_path)
    print(f"Written     : {output_path} ({size // 1024} KB)")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)

    input_path  = sys.argv[1]
    output_path = sys.argv[2]
    fade_in_ms  = int(sys.argv[3]) if len(sys.argv) > 3 else 500
    fade_out_ms = int(sys.argv[4]) if len(sys.argv) > 4 else 1500

    fade_wav(input_path, output_path, fade_in_ms, fade_out_ms)