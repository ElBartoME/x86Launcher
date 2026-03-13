/* fli.h, FLI/FLC animation player for x86Launcher.

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef __HAS_FLI
#define __HAS_FLI

#include <stdio.h>

// -------------------------------------------------------
// FLI/FLC magic numbers
// -------------------------------------------------------
#define FLI_MAGIC_FLI 0xAF11 // Original FLI format (320x200 only)
#define FLI_MAGIC_FLC 0xAF12 // FLC format (any resolution)

// -------------------------------------------------------
// FLI chunk type identifiers
// -------------------------------------------------------
#define FLI_CHUNK_COLOR_256 4  // 256-colour palette chunk
#define FLI_CHUNK_DELTA_FLC 7  // FLC line-by-line delta chunk
#define FLI_CHUNK_COLOR_64 11  // 64-level colour palette chunk (FLI only)
#define FLI_CHUNK_DELTA_FLI 12 // FLI delta chunk (word-oriented)
#define FLI_CHUNK_BLACK 13     // Set entire frame to colour 0
#define FLI_CHUNK_BYTE_RUN 15  // Full frame RLE compression
#define FLI_CHUNK_COPY 16      // Uncompressed full frame
#define FLI_CHUNK_MINI 18      // Thumbnail - ignore

// -------------------------------------------------------
// Return codes
// -------------------------------------------------------
#define FLI_OK 0
#define FLI_ERR_FILE -1        // Cannot open file
#define FLI_ERR_MAGIC -2       // Not a FLI/FLC file
#define FLI_ERR_MEM -3         // Memory allocation failed
#define FLI_ERR_READ -4        // File read error
#define FLI_ERR_UNSUPPORTED -5 // Unsupported feature

// -------------------------------------------------------
// How many palette entries the video is allowed to use.
// Must not exceed PALETTES_FREE (208) from palette.h so
// that UI palette entries (208-255) are left untouched.
// -------------------------------------------------------
#define FLI_MAX_COLOURS 208

// -------------------------------------------------------
// FLI file header - 128 bytes
// -------------------------------------------------------
typedef struct fli_header {
  unsigned long size;          // Size of the entire file in bytes
  unsigned short magic;        // 0xAF11 = FLI, 0xAF12 = FLC
  unsigned short frames;       // Number of frames
  unsigned short width;        // Width in pixels
  unsigned short height;       // Height in pixels
  unsigned short depth;        // Colour depth (always 8)
  unsigned short flags;        // Flags (unused)
  unsigned long speed;         // FLI: jiffies/frame (1/70s). FLC: ms/frame
  unsigned char reserved[110]; // Padding to 128 bytes
} __attribute__((packed)) fli_header_t;

// -------------------------------------------------------
// Frame header - 16 bytes, precedes each frame's chunks
// -------------------------------------------------------
typedef struct fli_frame_header {
  unsigned long size;    // Size of this frame including header
  unsigned short magic;  // Always 0xF1FA
  unsigned short chunks; // Number of chunks in this frame
  unsigned char reserved[8];
} __attribute__((packed)) fli_frame_header_t;

// -------------------------------------------------------
// Chunk header - 6 bytes, precedes each chunk's data
// -------------------------------------------------------
typedef struct fli_chunk_header {
  unsigned long size;  // Size of this chunk including header
  unsigned short type; // Chunk type identifier
} __attribute__((packed)) fli_chunk_header_t;

// -------------------------------------------------------
// State held between fli_Open and fli_Close
// -------------------------------------------------------
typedef struct fli_state {
  FILE *f;                      // Open file handle
  fli_header_t header;          // Parsed file header
  unsigned short current_frame; // Frame counter
  long first_frame_offset;      // File offset of frame 0 (for looping)
  // Local pixel buffer: one decoded frame at native FLI size
  unsigned char *pixels; // width * height bytes
} fli_state_t;

// -------------------------------------------------------
// Function prototypes
// -------------------------------------------------------

// Open a FLI/FLC file and read its header.
// Returns FLI_OK on success or a FLI_ERR_* code on failure.
int fli_Open(const char *path, fli_state_t *state);

// Close a previously opened FLI file and free pixel buffer.
void fli_Close(fli_state_t *state);

// Decode the next frame into state->pixels and update the
// hardware palette for entries 0..FLI_MAX_COLOURS-1 only.
// Returns FLI_OK, or FLI_ERR_READ at end of file / error.
int fli_NextFrame(fli_state_t *state);

// Rewind to the first frame so the clip can loop.
int fli_Rewind(fli_state_t *state);

// Play a FLI/FLC file in the artwork window, returning when
// the clip ends (loops == 0 means loop forever) or when the
// user presses any key.  Blits each decoded frame into the
// vram_buffer sub-region and calls gfx_Flip().
// dst_x / dst_y are the top-left corner of the target window.
// dst_w / dst_h are the dimensions of that window.
int fli_PlayPreview(const char *path, int dst_x, int dst_y, int dst_w,
                    int dst_h, int loops);

#endif /* __HAS_FLI */