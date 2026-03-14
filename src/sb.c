/* sb.c - Sound Blaster 8-bit auto-init DMA streaming WAV playback.
   Double-buffer: 16KB in conventional memory, full WAV in extended memory.
   sb_Tick() tracks which half is playing using uclock() and refills the idle half. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dos.h>
#include <dpmi.h>
#include <go32.h>
#include <pc.h>

#include "watcom_compat.h"
#include "sb.h"

/* -------------------------------------------------------
   Buffer sizes
   ------------------------------------------------------- */
#define HALF_SIZE   8192
#define BUF_SIZE    (HALF_SIZE * 2)

/* -------------------------------------------------------
   DSP port offsets
   ------------------------------------------------------- */
#define SB_RESET            0x06
#define SB_READ_DATA        0x0A
#define SB_WRITE_DATA       0x0C
#define SB_READ_STATUS      0x0E
#define SB_ACK_8BIT         0x0E

/* DSP commands */
#define DSP_SET_RATE        0x40
#define DSP_SET_BLOCKSIZE   0x48
#define DSP_DMA_8BIT_AI     0x1C   /* 8-bit auto-init output */
#define DSP_SPEAKER_ON      0xD1
#define DSP_SPEAKER_OFF     0xD3
#define DSP_HALT_DMA        0xD0

/* 8237 DMA registers */
static const unsigned char dma_page_reg[4]  = { 0x87, 0x83, 0x81, 0x82 };
static const unsigned char dma_addr_reg[4]  = { 0x00, 0x02, 0x04, 0x06 };
static const unsigned char dma_count_reg[4] = { 0x01, 0x03, 0x05, 0x07 };
#define DMA_MASK_REG        0x0A
#define DMA_MODE_REG        0x0B
#define DMA_FLIP_FLOP       0x0C
#define DMA_MODE_AUTOINIT   0x58   /* auto-init, read, channel bits OR'd in */

/* -------------------------------------------------------
   Internal state
   ------------------------------------------------------- */
static unsigned short sb_port  = 0x220;
static unsigned char  sb_irq   = 5;
static unsigned char  sb_dma   = 1;
static int            sb_ready = 0;
static int            playing  = 0;

/* Conventional memory double-buffer */
static _go32_dpmi_seginfo  dma_seg;
static unsigned char      *dma_buf;
static unsigned long       dma_phys;

/* WAV data in extended memory */
static unsigned char *wav_base     = NULL;
static unsigned char *wav_data     = NULL;
static unsigned long  wav_len      = 0;
static unsigned long  wav_pos      = 0;
static unsigned short wav_channels = 1;
static unsigned long  wav_rate     = 22050;
static int            wav_loaded   = 0;

/* Timing - use unsigned long long explicitly since uclock_t may be 64-bit
   but printf %lu only shows 32 bits on a 32-bit system */
static unsigned long long half_duration;   /* uclock ticks per half-buffer */
static unsigned long long play_start;      /* uclock when sb_Play was called */
static int                current_half;    /* which half is currently playing */

/* -------------------------------------------------------
   Helpers
   ------------------------------------------------------- */
static unsigned short sb_read16(const unsigned char *p) {
    return (unsigned short)p[0] | (unsigned short)p[1] << 8;
}

static unsigned long sb_read32(const unsigned char *p) {
    return (unsigned long)p[0]        | (unsigned long)p[1] << 8
         | (unsigned long)p[2] << 16  | (unsigned long)p[3] << 24;
}

static unsigned long find_chunk(const unsigned char *data, unsigned long len,
                                const char *tag, unsigned long *out_size) {
    unsigned long i = 12;
    while (i + 8 <= len) {
        unsigned long chunk_size = sb_read32(data + i + 4);
        if (memcmp(data + i, tag, 4) == 0) {
            *out_size = chunk_size;
            return i + 8;
        }
        i += 8 + chunk_size;
        if (chunk_size & 1) i++;
    }
    return 0;
}

static void dsp_write(unsigned char val) {
    while (inportb(sb_port + SB_WRITE_DATA) & 0x80)
        ;
    outportb(sb_port + SB_WRITE_DATA, val);
}

static int dsp_reset(void) {
    int i;
    outportb(sb_port + SB_RESET, 1);
    for (i = 0; i < 255; i++) inportb(sb_port + SB_RESET);
    outportb(sb_port + SB_RESET, 0);
    for (i = 0; i < 2000; i++) {
        if (inportb(sb_port + SB_READ_STATUS) & 0x80)
            if (inportb(sb_port + SB_READ_DATA) == 0xAA)
                return SB_OK;
    }
    return SB_ERR_NORESET;
}

static void fill_half(int half) {
    unsigned char *dst       = dma_buf + (half * HALF_SIZE);
    unsigned long  remaining = HALF_SIZE;
    unsigned long  to_copy;
    while (remaining > 0) {
        to_copy = wav_len - wav_pos;
        if (to_copy > remaining) to_copy = remaining;
        memcpy(dst, wav_data + wav_pos, to_copy);
        dst       += to_copy;
        wav_pos   += to_copy;
        remaining -= to_copy;
        if (wav_pos >= wav_len)
            wav_pos = 0;
    }
}

/* -------------------------------------------------------
   Public API
   ------------------------------------------------------- */

int sb_Init(void) {
    char *blaster, *p;

    printf("sb_Init: starting\n"); fflush(stdout);
    printf("sb_Init: sizeof(uclock_t)=%d\n", (int)sizeof(uclock_t)); fflush(stdout);
    printf("sb_Init: UCLOCKS_PER_SEC=%llu\n", (unsigned long long)UCLOCKS_PER_SEC); fflush(stdout);

    blaster = getenv("BLASTER");
    if (blaster != NULL) {
        printf("sb_Init: BLASTER=%s\n", blaster); fflush(stdout);
        p = blaster;
        while (*p) {
            switch (*p) {
            case 'A': case 'a':
                sb_port = (unsigned short)strtol(p+1, &p, 16); break;
            case 'I': case 'i':
                sb_irq  = (unsigned char)strtol(p+1, &p, 10);  break;
            case 'D': case 'd':
                sb_dma  = (unsigned char)strtol(p+1, &p, 10);  break;
            default:
                p++; break;
            }
        }
    } else {
        printf("sb_Init: no BLASTER env var, using defaults\n"); fflush(stdout);
    }

    printf("sb_Init: port=0x%03X irq=%d dma=%d\n", sb_port, sb_irq, sb_dma); fflush(stdout);

    if (sb_dma > 3) {
        printf("sb_Init: DMA channel %d not supported\n", sb_dma); fflush(stdout);
        return SB_ERR_NODMA;
    }

    if (dsp_reset() != SB_OK) {
        printf("sb_Init: DSP reset failed - no SB present\n"); fflush(stdout);
        return SB_ERR_NORESET;
    }
    printf("sb_Init: DSP reset OK\n"); fflush(stdout);

    /* Allocate 16KB double-buffer in conventional memory */
    dma_seg.size = (BUF_SIZE + 15) / 16;
    printf("sb_Init: allocating %d paragraphs (%d bytes) in conv mem\n",
           dma_seg.size, BUF_SIZE); fflush(stdout);

    if (_go32_dpmi_allocate_dos_memory(&dma_seg) != 0) {
        printf("sb_Init: conv mem allocation failed\n"); fflush(stdout);
        return SB_ERR_MEM;
    }

    dma_phys = (unsigned long)dma_seg.rm_segment << 4;
    dma_buf  = (unsigned char *)((unsigned long)__djgpp_conventional_base + dma_phys);

    printf("sb_Init: DMA buffer phys=0x%08lX flat=0x%08lX\n",
           dma_phys, (unsigned long)dma_buf); fflush(stdout);

    sb_ready = 1;
    printf("sb_Init: success\n"); fflush(stdout);
    return SB_OK;
}

int sb_LoadWAV(const char *path) {
    FILE         *f;
    unsigned long file_size;
    unsigned long fmt_off, fmt_size;
    unsigned long data_off, data_size;

    printf("sb_LoadWAV: %s\n", path); fflush(stdout);

    if (!sb_ready) {
        printf("sb_LoadWAV: sb not ready\n"); fflush(stdout);
        return SB_ERR_NORESET;
    }

    sb_Stop();

    if (wav_loaded) {
        printf("sb_LoadWAV: freeing previous WAV\n"); fflush(stdout);
        free(wav_base);
        wav_base = NULL;
        wav_data = NULL;
        wav_loaded = 0;
    }

    f = fopen(path, "rb");
    if (f == NULL) {
        printf("sb_LoadWAV: fopen failed\n"); fflush(stdout);
        return SB_ERR_FILE;
    }

    fseek(f, 0, SEEK_END);
    file_size = (unsigned long)ftell(f);
    fseek(f, 0, SEEK_SET);
    printf("sb_LoadWAV: file size = %lu bytes\n", file_size); fflush(stdout);

    wav_base = (unsigned char *)malloc(file_size);
    if (wav_base == NULL) {
        printf("sb_LoadWAV: malloc failed for %lu bytes\n", file_size); fflush(stdout);
        fclose(f);
        return SB_ERR_MEM;
    }

    if (fread(wav_base, 1, file_size, f) != file_size) {
        printf("sb_LoadWAV: fread failed\n"); fflush(stdout);
        fclose(f); free(wav_base); wav_base = NULL;
        return SB_ERR_BADWAV;
    }
    fclose(f);

    if (memcmp(wav_base, "RIFF", 4) != 0 || memcmp(wav_base + 8, "WAVE", 4) != 0) {
        printf("sb_LoadWAV: not a RIFF/WAVE file\n"); fflush(stdout);
        free(wav_base); wav_base = NULL;
        return SB_ERR_BADWAV;
    }

    fmt_off = find_chunk(wav_base, file_size, "fmt ", &fmt_size);
    if (fmt_off == 0) {
        printf("sb_LoadWAV: no fmt chunk\n"); fflush(stdout);
        free(wav_base); wav_base = NULL;
        return SB_ERR_BADWAV;
    }

    if (sb_read16(wav_base + fmt_off) != 1) {
        printf("sb_LoadWAV: not PCM format (tag=%d)\n", sb_read16(wav_base + fmt_off)); fflush(stdout);
        free(wav_base); wav_base = NULL;
        return SB_ERR_BADWAV;
    }

    wav_channels = sb_read16(wav_base + fmt_off + 2);
    wav_rate     = sb_read32(wav_base + fmt_off + 4);

    printf("sb_LoadWAV: channels=%d rate=%lu bits=%d\n",
           wav_channels, wav_rate, sb_read16(wav_base + fmt_off + 14)); fflush(stdout);

    if (sb_read16(wav_base + fmt_off + 14) != 8) {
        printf("sb_LoadWAV: not 8-bit PCM\n"); fflush(stdout);
        free(wav_base); wav_base = NULL;
        return SB_ERR_BADWAV;
    }

    data_off = find_chunk(wav_base, file_size, "data", &data_size);
    if (data_off == 0) {
        printf("sb_LoadWAV: no data chunk\n"); fflush(stdout);
        free(wav_base); wav_base = NULL;
        return SB_ERR_BADWAV;
    }

    wav_data   = wav_base + data_off;
    wav_len    = data_size;
    wav_pos    = 0;
    wav_loaded = 1;

    printf("sb_LoadWAV: PCM data offset=%lu size=%lu (%.1f sec)\n",
           data_off, data_size,
           (float)data_size / (float)(wav_rate * wav_channels)); fflush(stdout);

    return SB_OK;
}

int sb_Play(void) {
    unsigned char tc;
    unsigned long long uclocks_per_sec;

    (void)uclock();
    (void)uclock();
    play_start = (unsigned long long)uclock();

    printf("sb_Play: starting\n"); fflush(stdout);

    if (!sb_ready) { printf("sb_Play: not ready\n"); fflush(stdout); return SB_ERR_NORESET; }
    if (!wav_loaded) { printf("sb_Play: no WAV loaded\n"); fflush(stdout); return SB_ERR_NORESET; }

    if (dsp_reset() != SB_OK) {
        printf("sb_Play: DSP reset failed\n"); fflush(stdout);
        return SB_ERR_NORESET;
    }

    wav_pos = 0;

    /* Pre-fill both halves */
    fill_half(0);
    fill_half(1);
    printf("sb_Play: both halves pre-filled\n"); fflush(stdout);

    /* Time constant: 256 - (1000000 / (channels * rate)) */
    tc = (unsigned char)(256 - (1000000UL / ((unsigned long)wav_channels * wav_rate)));
    printf("sb_Play: time constant = %d\n", (int)tc); fflush(stdout);

    /* Calculate half_duration in uclock ticks using 64-bit arithmetic */
    uclocks_per_sec = (unsigned long long)UCLOCKS_PER_SEC;
    half_duration   = (unsigned long long)HALF_SIZE * uclocks_per_sec
                    / ((unsigned long long)wav_rate * (unsigned long long)wav_channels);

    printf("sb_Play: UCLOCKS_PER_SEC=%llu\n", uclocks_per_sec); fflush(stdout);
    printf("sb_Play: HALF_SIZE=%d rate=%lu ch=%d\n", HALF_SIZE, wav_rate, wav_channels); fflush(stdout);
    printf("sb_Play: half_duration=%llu uclock ticks (%.3f sec)\n",
           half_duration,
           (float)half_duration / (float)uclocks_per_sec); fflush(stdout);

    if (half_duration == 0) {
        printf("sb_Play: ERROR - half_duration is 0, aborting\n"); fflush(stdout);
        return SB_ERR_NORESET;
    }

    /* Program 8237 for auto-init over full BUF_SIZE */
    outportb(DMA_MASK_REG,  0x04 | sb_dma);
    outportb(DMA_FLIP_FLOP, 0x00);
    outportb(DMA_MODE_REG,  DMA_MODE_AUTOINIT | sb_dma);
    outportb(dma_addr_reg[sb_dma], (unsigned char)( dma_phys        & 0xFF));
    outportb(dma_addr_reg[sb_dma], (unsigned char)((dma_phys >>  8) & 0xFF));
    outportb(dma_page_reg[sb_dma], (unsigned char)((dma_phys >> 16) & 0xFF));
    outportb(dma_count_reg[sb_dma], (unsigned char)((BUF_SIZE - 1) & 0xFF));
    outportb(dma_count_reg[sb_dma], (unsigned char)((BUF_SIZE - 1) >> 8));
    outportb(DMA_MASK_REG, sb_dma);
    printf("sb_Play: DMA programmed\n"); fflush(stdout);

    /* DSP: speaker on, time constant, block size, start auto-init */
    dsp_write(DSP_SPEAKER_ON);
    dsp_write(DSP_SET_RATE);
    dsp_write(tc);
    dsp_write(DSP_SET_BLOCKSIZE);
    dsp_write((unsigned char)((HALF_SIZE - 1) & 0xFF));
    dsp_write((unsigned char)((HALF_SIZE - 1) >> 8));
    dsp_write(DSP_DMA_8BIT_AI);
    printf("sb_Play: DSP started\n"); fflush(stdout);

    current_half = 0;
    play_start   = (unsigned long long)uclock();

    playing = 1;
    printf("sb_Play: playing. play_start=%llu\n", play_start); fflush(stdout);
    return SB_OK;
}

void sb_Stop(void) {
    if (!playing) return;
    printf("sb_Stop\n"); fflush(stdout);
    dsp_write(DSP_HALT_DMA);
    dsp_write(DSP_SPEAKER_OFF);
    playing = 0;
}

void sb_Tick(void) {
    unsigned long long now;
    unsigned long long elapsed;
    unsigned long long half_index;
    int                should_be_half;
    int                idle_half;
    static int         tick_count   = 0;
    static int         fill_count   = 0;

    if (!playing || !sb_ready || !wav_loaded)
        return;

    if (half_duration == 0) {
        printf("sb_Tick: ERROR half_duration=0, stopping\n"); fflush(stdout);
        sb_Stop();
        return;
    }

    tick_count++;
    now     = (unsigned long long)uclock();
    elapsed = now - play_start;

    /* Log every 500 ticks */
    if (tick_count % 500 == 0) {
        printf("sb_Tick: count=%d fills=%d half=%d elapsed=%llu duration=%llu ratio=%.3f\n",
               tick_count, fill_count, current_half,
               elapsed, half_duration,
               (float)elapsed / (float)half_duration); fflush(stdout);
    }

    /* Determine which half should be playing now */
    half_index     = elapsed / half_duration;
    should_be_half = (int)(half_index & 1);   /* even=half0, odd=half1 */
    idle_half      = 1 - should_be_half;

    if (should_be_half != current_half) {
        fill_count++;
        printf("sb_Tick: crossing to half %d (fill #%d) elapsed=%llu\n",
               should_be_half, fill_count, elapsed); fflush(stdout);

        /* Acknowledge DSP interrupt */
        inportb(sb_port + SB_ACK_8BIT);

        /* Refill the half we just left */
        fill_half(idle_half);

        current_half = should_be_half;
    }
}

void sb_Shutdown(void) {
    printf("sb_Shutdown\n"); fflush(stdout);
    sb_Stop();
    if (wav_loaded) {
        free(wav_base);
        wav_base   = NULL;
        wav_data   = NULL;
        wav_loaded = 0;
    }
    if (sb_ready) {
        _go32_dpmi_free_dos_memory(&dma_seg);
        sb_ready = 0;
    }
}

int sb_IsPlaying(void) {
    return playing;
}