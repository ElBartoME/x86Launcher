/* fli.c, FLI/FLC animation player for x86Launcher.

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

#include <conio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


#include "fli.h"

#ifndef __HAS_GFX
#include "gfx.h"
#define __HAS_GFX
#endif

#ifndef __HAS_PAL
#include "palette.h"
#define __HAS_PAL
#endif

// vram_buffer is defined in gfx.c - we write frames directly into it
extern unsigned char vram_buffer[];

// -------------------------------------------------------
// Internal helpers
// -------------------------------------------------------

// Apply a COLOR_256 palette chunk to hardware registers.
// Palette entries are clamped to FLI_MAX_COLOURS so we never
// overwrite the UI palette reserved at entries 208-255.
static int fli_apply_palette_256(fli_state_t *state,
                                 unsigned long chunk_data_size) {

  unsigned short num_packets;
  unsigned char skip_count;
  unsigned char change_count;
  unsigned char rgb[3];
  int entry;
  int i;
  int packets_read;

  if (fread(&num_packets, 2, 1, state->f) != 1)
    return FLI_ERR_READ;

  entry = 0;
  for (packets_read = 0; packets_read < num_packets; packets_read++) {

    // Skip count: number of entries to skip
    if (fread(&skip_count, 1, 1, state->f) != 1)
      return FLI_ERR_READ;
    entry += skip_count;

    // Change count: 0 means 256
    if (fread(&change_count, 1, 1, state->f) != 1)
      return FLI_ERR_READ;
    int n = (change_count == 0) ? 256 : (int)change_count;

    for (i = 0; i < n; i++, entry++) {
      if (fread(rgb, 1, 3, state->f) != 3)
        return FLI_ERR_READ;
      // Only write into the free palette region
      if (entry < FLI_MAX_COLOURS) {
        pal_Set((unsigned char)entry, rgb[0], rgb[1], rgb[2]);
      }
    }
  }
  return FLI_OK;
}

// Apply a COLOR_64 palette chunk (FLI format, 6-bit values 0-63).
// Same entry clamping as above.
static int fli_apply_palette_64(fli_state_t *state,
                                unsigned long chunk_data_size) {

  unsigned short num_packets;
  unsigned char skip_count;
  unsigned char change_count;
  unsigned char rgb[3];
  int entry;
  int i;
  int packets_read;

  if (fread(&num_packets, 2, 1, state->f) != 1)
    return FLI_ERR_READ;

  entry = 0;
  for (packets_read = 0; packets_read < num_packets; packets_read++) {
    if (fread(&skip_count, 1, 1, state->f) != 1)
      return FLI_ERR_READ;
    entry += skip_count;
    if (fread(&change_count, 1, 1, state->f) != 1)
      return FLI_ERR_READ;
    int n = (change_count == 0) ? 256 : (int)change_count;
    for (i = 0; i < n; i++, entry++) {
      if (fread(rgb, 1, 3, state->f) != 3)
        return FLI_ERR_READ;
      // Scale 6-bit (0-63) to 8-bit (0-255)
      if (entry < FLI_MAX_COLOURS) {
        pal_Set((unsigned char)entry, (unsigned char)(rgb[0] << 2),
                (unsigned char)(rgb[1] << 2), (unsigned char)(rgb[2] << 2));
      }
    }
  }
  return FLI_OK;
}

// Decode a BYTE_RUN chunk (full-frame RLE) into state->pixels.
static int fli_decode_byte_run(fli_state_t *state) {

  int row, col;
  unsigned char *dst;
  signed char count;
  unsigned char value;
  int w = (int)state->header.width;
  int h = (int)state->header.height;

  dst = state->pixels;
  for (row = 0; row < h; row++) {
    col = 0;
    // First byte of each row is a packet count (ignored in practice,
    // we just decode until we've filled the row width).
    unsigned char packet_count;
    if (fread(&packet_count, 1, 1, state->f) != 1)
      return FLI_ERR_READ;

    while (col < w) {
      if (fread(&count, 1, 1, state->f) != 1)
        return FLI_ERR_READ;
      if (count < 0) {
        // Literal run: -count bytes follow verbatim
        int n = (int)(-count);
        if (fread(dst + col, 1, n, state->f) != (size_t)n)
          return FLI_ERR_READ;
        col += n;
      } else {
        // Repeat run: one byte repeated count times
        if (fread(&value, 1, 1, state->f) != 1)
          return FLI_ERR_READ;
        memset(dst + col, value, (size_t)count);
        col += (int)count;
      }
    }
    dst += w;
  }
  return FLI_OK;
}

// Decode a DELTA_FLI chunk (word-oriented delta, original FLI format).
static int fli_decode_delta_fli(fli_state_t *state) {

  unsigned short num_lines;
  unsigned short skip_lines;
  signed char num_packets;
  unsigned char skip_cols;
  signed char count;
  unsigned char pair[2];
  int row, col, i;
  unsigned char *dst;
  int w = (int)state->header.width;

  if (fread(&skip_lines, 2, 1, state->f) != 1)
    return FLI_ERR_READ;
  if (fread(&num_lines, 2, 1, state->f) != 1)
    return FLI_ERR_READ;

  row = (int)skip_lines;
  int end_row = row + (int)num_lines;

  while (row < end_row) {
    dst = state->pixels + row * w;
    if (fread(&num_packets, 1, 1, state->f) != 1)
      return FLI_ERR_READ;
    col = 0;
    for (i = 0; i < (int)num_packets; i++) {
      if (fread(&skip_cols, 1, 1, state->f) != 1)
        return FLI_ERR_READ;
      col += (int)skip_cols;
      if (fread(&count, 1, 1, state->f) != 1)
        return FLI_ERR_READ;
      if (count > 0) {
        // Literal word pairs
        int n = (int)count;
        if (fread(dst + col, 1, n * 2, state->f) != (size_t)(n * 2))
          return FLI_ERR_READ;
        col += n * 2;
      } else if (count < 0) {
        // Repeated word pair
        int n = (int)(-count);
        if (fread(pair, 1, 2, state->f) != 2)
          return FLI_ERR_READ;
        for (int j = 0; j < n; j++) {
          dst[col++] = pair[0];
          dst[col++] = pair[1];
        }
      }
    }
    row++;
  }
  return FLI_OK;
}

// Decode a DELTA_FLC chunk (byte-oriented line delta, FLC format).
static int fli_decode_delta_flc(fli_state_t *state) {

  unsigned short num_lines;
  short line_word;
  unsigned char skip_cols;
  signed char count;
  unsigned char value;
  int row, col, i;
  unsigned char *dst;
  int w = (int)state->header.width;
  int h = (int)state->header.height;

  if (fread(&num_lines, 2, 1, state->f) != 1)
    return FLI_ERR_READ;

  row = 0;
  int lines_done = 0;

  while (lines_done < (int)num_lines) {
    // Read an opcode word
    if (fread(&line_word, 2, 1, state->f) != 1)
      return FLI_ERR_READ;

    if ((line_word & 0xC000) == 0x0000) {
      // Packet count for this line
      int num_packets = (int)line_word;
      dst = state->pixels + row * w;
      col = 0;
      for (i = 0; i < num_packets; i++) {
        if (fread(&skip_cols, 1, 1, state->f) != 1)
          return FLI_ERR_READ;
        col += (int)skip_cols;
        if (fread(&count, 1, 1, state->f) != 1)
          return FLI_ERR_READ;
        if (count > 0) {
          // Literal run
          if (fread(dst + col, 1, (size_t)count, state->f) != (size_t)count)
            return FLI_ERR_READ;
          col += (int)count;
        } else if (count < 0) {
          // Repeat run
          if (fread(&value, 1, 1, state->f) != 1)
            return FLI_ERR_READ;
          memset(dst + col, value, (size_t)(-count));
          col += (int)(-count);
        }
      }
      row++;
      lines_done++;
    } else if ((line_word & 0xC000) == 0xC000) {
      // Skip lines (negative value = number of lines to skip)
      row += (int)(-(short)line_word);
    }
    // 0x8000 = set last byte of previous line; rare, skip
  }
  return FLI_OK;
}

// Decode a COPY chunk (raw uncompressed frame).
static int fli_decode_copy(fli_state_t *state) {
  int total = (int)state->header.width * (int)state->header.height;
  if (fread(state->pixels, 1, (size_t)total, state->f) != (size_t)total)
    return FLI_ERR_READ;
  return FLI_OK;
}

// Blit state->pixels (native FLI frame) into the vram_buffer
// sub-region defined by dst_x/dst_y/dst_w/dst_h, centred and
// clipped to fit.
static void fli_blit(fli_state_t *state, int dst_x, int dst_y, int dst_w,
                     int dst_h) {

  int src_w = (int)state->header.width;
  int src_h = (int)state->header.height;

  // Centre the frame inside the artwork window
  int off_x = (dst_w - src_w) / 2;
  int off_y = (dst_h - src_h) / 2;
  if (off_x < 0)
    off_x = 0;
  if (off_y < 0)
    off_y = 0;

  int blit_w = src_w;
  int blit_h = src_h;
  if (blit_w > dst_w)
    blit_w = dst_w;
  if (blit_h > dst_h)
    blit_h = dst_h;

  int screen_x = dst_x + off_x;
  int screen_y = dst_y + off_y;

  int row;
  for (row = 0; row < blit_h; row++) {
    unsigned char *src = state->pixels + row * src_w;
    unsigned char *dst = vram_buffer + (screen_y + row) * GFX_COLS + screen_x;
    memcpy(dst, src, (size_t)blit_w);
  }
}

// -------------------------------------------------------
// Public API
// -------------------------------------------------------

int fli_Open(const char *path, fli_state_t *state) {

  unsigned char hdr[128];

  memset(state, 0, sizeof(fli_state_t));

  state->f = fopen(path, "rb");
  if (state->f == NULL)
    return FLI_ERR_FILE;

  if (fread(hdr, 1, 128, state->f) != 128) {
    fclose(state->f);
    state->f = NULL;
    return FLI_ERR_READ;
  }

  // Extract fields at their exact byte offsets - no struct padding issues
  state->header.size   = (unsigned long)hdr[0]
                       | (unsigned long)hdr[1] << 8
                       | (unsigned long)hdr[2] << 16
                       | (unsigned long)hdr[3] << 24;
  state->header.magic  = (unsigned short)hdr[4]
                       | (unsigned short)hdr[5] << 8;
  state->header.frames = (unsigned short)hdr[6]
                       | (unsigned short)hdr[7] << 8;
  state->header.width  = (unsigned short)hdr[8]
                       | (unsigned short)hdr[9] << 8;
  state->header.height = (unsigned short)hdr[10]
                       | (unsigned short)hdr[11] << 8;
  state->header.depth  = (unsigned short)hdr[12]
                       | (unsigned short)hdr[13] << 8;
  state->header.flags  = (unsigned short)hdr[14]
                       | (unsigned short)hdr[15] << 8;
  state->header.speed  = (unsigned long)hdr[16]
                       | (unsigned long)hdr[17] << 8
                       | (unsigned long)hdr[18] << 16
                       | (unsigned long)hdr[19] << 24;

  if (state->header.magic != FLI_MAGIC_FLI &&
      state->header.magic != FLI_MAGIC_FLC) {
    fclose(state->f);
    state->f = NULL;
    return FLI_ERR_MAGIC;
  }

  int pixels = (int)state->header.width * (int)state->header.height;
  state->pixels = (unsigned char *)malloc((size_t)pixels);
  if (state->pixels == NULL) {
    fclose(state->f);
    state->f = NULL;
    return FLI_ERR_MEM;
  }

  memset(state->pixels, 0, (size_t)pixels);
  state->current_frame = 0;
  state->first_frame_offset = 128L;

  return FLI_OK;
}

void fli_Close(fli_state_t *state) {
  if (state->f != NULL) {
    fclose(state->f);
    state->f = NULL;
  }
  if (state->pixels != NULL) {
    free(state->pixels);
    state->pixels = NULL;
  }
}

int fli_Rewind(fli_state_t *state) {
  if (fseek(state->f, state->first_frame_offset, SEEK_SET) != 0)
    return FLI_ERR_READ;
  state->current_frame = 0;
  return FLI_OK;
}

int fli_NextFrame(fli_state_t *state) {

  unsigned char buf[16];   /* big enough for both frame (16) and chunk (6) headers */
  unsigned long  frame_size;
  unsigned short frame_magic, frame_chunks;
  unsigned long  chunk_size;
  unsigned short chunk_type;
  long frame_start;
  long chunk_start;
  int c;
  int status;

  if (state->current_frame >= state->header.frames)
    return FLI_ERR_READ;

  frame_start = ftell(state->f);
  if (fread(buf, 1, 16, state->f) != 16)
    return FLI_ERR_READ;

  frame_size   = (unsigned long)buf[0]  | (unsigned long)buf[1]  << 8
               | (unsigned long)buf[2]  << 16 | (unsigned long)buf[3] << 24;
  frame_magic  = (unsigned short)buf[4] | (unsigned short)buf[5] << 8;
  frame_chunks = (unsigned short)buf[6] | (unsigned short)buf[7] << 8;

  /* 0xF1FA is the only valid frame magic.
     Some encoders emit a prefix header frame; skip it and read the real one. */
  if (frame_magic != 0xF1FA) {
    if (fseek(state->f, frame_start + (long)frame_size, SEEK_SET) != 0)
      return FLI_ERR_READ;
    frame_start = ftell(state->f);
    if (fread(buf, 1, 16, state->f) != 16)
      return FLI_ERR_READ;
    frame_size   = (unsigned long)buf[0]  | (unsigned long)buf[1]  << 8
                 | (unsigned long)buf[2]  << 16 | (unsigned long)buf[3] << 24;
    frame_magic  = (unsigned short)buf[4] | (unsigned short)buf[5] << 8;
    frame_chunks = (unsigned short)buf[6] | (unsigned short)buf[7] << 8;
    if (frame_magic != 0xF1FA)
      return FLI_ERR_READ;
  }

  for (c = 0; c < (int)frame_chunks; c++) {

    chunk_start = ftell(state->f);
    if (fread(buf, 1, 6, state->f) != 6)
      return FLI_ERR_READ;

    chunk_size = (unsigned long)buf[0]  | (unsigned long)buf[1]  << 8
               | (unsigned long)buf[2]  << 16 | (unsigned long)buf[3] << 24;
    chunk_type = (unsigned short)buf[4] | (unsigned short)buf[5] << 8;

    switch (chunk_type) {

    case FLI_CHUNK_COLOR_256:
      status = fli_apply_palette_256(state, chunk_size - 6);
      if (status != FLI_OK)
        return status;
      break;

    case FLI_CHUNK_COLOR_64:
      status = fli_apply_palette_64(state, chunk_size - 6);
      if (status != FLI_OK)
        return status;
      break;

    case FLI_CHUNK_BYTE_RUN:
      status = fli_decode_byte_run(state);
      if (status != FLI_OK)
        return status;
      break;

    case FLI_CHUNK_DELTA_FLI:
      status = fli_decode_delta_fli(state);
      if (status != FLI_OK)
        return status;
      break;

    case FLI_CHUNK_DELTA_FLC:
      status = fli_decode_delta_flc(state);
      if (status != FLI_OK)
        return status;
      break;

    case FLI_CHUNK_COPY:
      status = fli_decode_copy(state);
      if (status != FLI_OK)
        return status;
      break;

    case FLI_CHUNK_BLACK:
      memset(state->pixels, 0,
             (size_t)(state->header.width * state->header.height));
      break;

    case FLI_CHUNK_MINI:
    default:
      break;
    }

    /* Always seek to the exact end of this chunk to stay aligned */
    if (fseek(state->f, chunk_start + (long)chunk_size, SEEK_SET) != 0)
      return FLI_ERR_READ;
  }

  state->current_frame++;
  return FLI_OK;
}

int fli_PlayPreview(const char *path, int dst_x, int dst_y, int dst_w,
                    int dst_h, int loops) {

  fli_state_t state;
  int status;
  int loop_count;
  int done;
  uclock_t frame_start;
  uclock_t frame_end;
  uclock_t target_uclock;

  status = fli_Open(path, &state);
  if (status != FLI_OK)
    return status;

  // FLI speed field is in 1/70s jiffies. Convert to microseconds.
  // FLC speed field is already in milliseconds.
  if (state.header.magic == FLI_MAGIC_FLI) {
    target_uclock = (uclock_t)state.header.speed * 1000000UL / 70UL;
  } else {
    target_uclock = (uclock_t)state.header.speed * 1000UL;
  }
  if (target_uclock < 1)
    target_uclock = 1;

  gfx_BoxFill(dst_x, dst_y, dst_x + dst_w, dst_y + dst_h, PALETTE_UI_BLACK);
  gfx_Flip();

  done       = 0;
  loop_count = 0;

  while (!done) {

    frame_start = uclock();

    status = fli_NextFrame(&state);

    if (status == FLI_ERR_READ) {
      loop_count++;
      if (loops == 0 || loop_count < loops) {
        if (fli_Rewind(&state) != FLI_OK) {
          done = 1;
          continue;
        }
        status = fli_NextFrame(&state);
        if (status != FLI_OK) {
          done = 1;
          continue;
        }
      } else {
        done = 1;
        continue;
      }
    } else if (status != FLI_OK) {
      done = 1;
      continue;
    }

    fli_blit(&state, dst_x, dst_y, dst_w, dst_h);
    gfx_Flip();

    if (kbhit()) {
      int key = getch();
      // Extended keys (arrows) come as two bytes: 0x00 then the scan code
      if (key == 0x00 || key == 0xE0) {
        int ext = getch();
        // Push both bytes back so the main loop can read them normally
        ungetch(ext);
        ungetch(key);
        done = 1;
      }
      // Any other key also stops playback but is consumed
    }

    // Spin-wait using hardware timer - CPU speed independent
    do {
      frame_end = uclock();
    } while ((frame_end - frame_start) < target_uclock);
  }

  fli_Close(&state);
  pal_ResetFree();

  return FLI_OK;
}