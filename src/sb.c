/* sb.c - Sound Blaster 8-bit auto-init DMA streaming WAV playback.
   Double-buffer: 16KB in conventional memory, full WAV in extended memory.
   sb_Tick() tracks which half is playing by reading the DMA counter register. */

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

/* WAV streaming state */
static FILE          *wav_file        = NULL;
static unsigned long  wav_data_offset = 0;
static unsigned long  wav_len         = 0;
static unsigned long  wav_pos         = 0;
static unsigned short wav_channels    = 1;
static unsigned long  wav_rate        = 22050;
static int            wav_loaded      = 0;

static int            current_half;    /* which half is currently playing */

static int            sb_volume       = 256;

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

static unsigned long find_chunk_file(FILE *f, unsigned long file_size,
                                     const char *tag, unsigned long *out_size) {
    unsigned char buf[8];
    fseek(f, 12, SEEK_SET);
    while (ftell(f) + 8 <= (long)file_size) {
        if (fread(buf, 1, 8, f) != 8) break;
        unsigned long chunk_size = sb_read32(buf + 4);
        if (memcmp(buf, tag, 4) == 0) {
            *out_size = chunk_size;
            return (unsigned long)ftell(f);
        }
        fseek(f, (long)chunk_size + (chunk_size & 1 ? 1 : 0), SEEK_CUR);
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
    unsigned long  to_read;
    unsigned long  got;

    while (remaining > 0) {
        to_read = wav_len - wav_pos;
        if (to_read > remaining) to_read = remaining;

        if (sb_volume == 256) {
            got = (unsigned long)fread(dst, 1, to_read, wav_file);
        } else {
            unsigned char tmp[HALF_SIZE];
            got = (unsigned long)fread(tmp, 1, to_read, wav_file);
            unsigned long i;
            for (i = 0; i < got; i++) {
                int s = (int)tmp[i] - 128;
                s = (s * sb_volume) >> 8;
                dst[i] = (unsigned char)(s + 128);
            }
        }

        dst       += got;
        wav_pos   += got;
        remaining -= got;

        if (wav_pos >= wav_len) {
            wav_pos = 0;
            fseek(wav_file, (long)wav_data_offset, SEEK_SET);
        }
    }
}

/* -------------------------------------------------------
   Public API
   ------------------------------------------------------- */

int sb_Init(void) {
    char *blaster, *p;

#if SB_VERBOSE
    printf("sb_Init: starting\n"); fflush(stdout);
    printf("sb_Init: sizeof(uclock_t)=%d\n", (int)sizeof(uclock_t)); fflush(stdout);
    printf("sb_Init: UCLOCKS_PER_SEC=%llu\n", (unsigned long long)UCLOCKS_PER_SEC); fflush(stdout);
#endif

    blaster = getenv("BLASTER");
    if (blaster != NULL) {
#if SB_VERBOSE
        printf("sb_Init: BLASTER=%s\n", blaster); fflush(stdout);
#endif
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
#if SB_VERBOSE
        printf("sb_Init: no BLASTER env var, using defaults\n"); fflush(stdout);
#endif
    }

#if SB_VERBOSE
    printf("sb_Init: port=0x%03X irq=%d dma=%d\n", sb_port, sb_irq, sb_dma); fflush(stdout);
#endif

    if (sb_dma > 3) {
#if SB_VERBOSE
        printf("sb_Init: DMA channel %d not supported\n", sb_dma); fflush(stdout);
#endif
        return SB_ERR_NODMA;
    }

    if (dsp_reset() != SB_OK) {
#if SB_VERBOSE
        printf("sb_Init: DSP reset failed - no SB present\n"); fflush(stdout);
#endif
        return SB_ERR_NORESET;
    }
#if SB_VERBOSE
    printf("sb_Init: DSP reset OK\n"); fflush(stdout);
#endif

    /* Allocate 16KB double-buffer in conventional memory */
    dma_seg.size = (BUF_SIZE + 15) / 16;
#if SB_VERBOSE
    printf("sb_Init: allocating %d paragraphs (%d bytes) in conv mem\n",
           dma_seg.size, BUF_SIZE); fflush(stdout);
#endif

    if (_go32_dpmi_allocate_dos_memory(&dma_seg) != 0) {
#if SB_VERBOSE
        printf("sb_Init: conv mem allocation failed\n"); fflush(stdout);
#endif
        return SB_ERR_MEM;
    }

    dma_phys = (unsigned long)dma_seg.rm_segment << 4;
    dma_buf  = (unsigned char *)((unsigned long)__djgpp_conventional_base + dma_phys);

#if SB_VERBOSE
    printf("sb_Init: DMA buffer phys=0x%08lX flat=0x%08lX\n",
           dma_phys, (unsigned long)dma_buf); fflush(stdout);
    printf("sb_Init: success\n"); fflush(stdout);
#endif

    sb_ready = 1;
    return SB_OK;
}

int sb_LoadWAV(const char *path) {
    FILE         *f;
    unsigned long file_size;
    unsigned long fmt_off, fmt_size;
    unsigned long data_off, data_size;

#if SB_VERBOSE
    printf("sb_LoadWAV: %s\n", path); fflush(stdout);
#endif

    if (!sb_ready) {
#if SB_VERBOSE
        printf("sb_LoadWAV: sb not ready\n"); fflush(stdout);
#endif
        return SB_ERR_NORESET;
    }

    /* Close any previously open WAV file */
    if (wav_file != NULL) {
        fclose(wav_file);
        wav_file   = NULL;
        wav_loaded = 0;
    }

    f = fopen(path, "rb");
    if (f == NULL) {
#if SB_VERBOSE
        printf("sb_LoadWAV: fopen failed\n"); fflush(stdout);
#endif
        return SB_ERR_FILE;
    }

    fseek(f, 0, SEEK_END);
    file_size = (unsigned long)ftell(f);
    fseek(f, 0, SEEK_SET);
#if SB_VERBOSE
    printf("sb_LoadWAV: file size = %lu bytes\n", file_size); fflush(stdout);
#endif

    /* Read only enough of the file to parse the RIFF/WAVE header.
       512 bytes is more than enough for fmt + data chunk headers. */
    unsigned char hdr[512];
    size_t hdr_read = fread(hdr, 1, sizeof(hdr), f);
    if (hdr_read < 12) {
#if SB_VERBOSE
        printf("sb_LoadWAV: file too small\n"); fflush(stdout);
#endif
        fclose(f);
        return SB_ERR_BADWAV;
    }

    if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) {
#if SB_VERBOSE
        printf("sb_LoadWAV: not a RIFF/WAVE file\n"); fflush(stdout);
#endif
        fclose(f);
        return SB_ERR_BADWAV;
    }

    /* Parse chunks from the header buffer */
    unsigned long i = 12;
    fmt_off  = 0; fmt_size  = 0;
    data_off = 0; data_size = 0;

    while (i + 8 <= hdr_read) {
        unsigned long chunk_size = sb_read32(hdr + i + 4);
        if (memcmp(hdr + i, "fmt ", 4) == 0) {
            fmt_off  = i + 8;
            fmt_size = chunk_size;
        } else if (memcmp(hdr + i, "data", 4) == 0) {
            data_off  = i + 8;
            data_size = chunk_size;
            break;
        }
        i += 8 + chunk_size;
        if (chunk_size & 1) i++;
    }

    if (fmt_off == 0) {
#if SB_VERBOSE
        printf("sb_LoadWAV: no fmt chunk in header\n"); fflush(stdout);
#endif
        fclose(f);
        return SB_ERR_BADWAV;
    }

    if (sb_read16(hdr + fmt_off) != 1) {
#if SB_VERBOSE
        printf("sb_LoadWAV: not PCM format (tag=%d)\n", sb_read16(hdr + fmt_off)); fflush(stdout);
#endif
        fclose(f);
        return SB_ERR_BADWAV;
    }

    wav_channels = sb_read16(hdr + fmt_off + 2);
    wav_rate     = sb_read32(hdr + fmt_off + 4);

#if SB_VERBOSE
    printf("sb_LoadWAV: channels=%d rate=%lu bits=%d\n",
           wav_channels, wav_rate, sb_read16(hdr + fmt_off + 14)); fflush(stdout);
#endif

    if (sb_read16(hdr + fmt_off + 14) != 8) {
#if SB_VERBOSE
        printf("sb_LoadWAV: not 8-bit PCM\n"); fflush(stdout);
#endif
        fclose(f);
        return SB_ERR_BADWAV;
    }

    /* If data chunk wasn't in the first 512 bytes, scan the full file */
    if (data_off == 0) {
        unsigned long scan_size;
        data_off = find_chunk_file(f, file_size, "data", &scan_size);
        if (data_off == 0) {
#if SB_VERBOSE
            printf("sb_LoadWAV: no data chunk\n"); fflush(stdout);
#endif
            fclose(f);
            return SB_ERR_BADWAV;
        }
        data_size = scan_size;
    }

    /* Seek to the start of PCM data and keep the file open for streaming */
    fseek(f, (long)data_off, SEEK_SET);

    wav_file        = f;
    wav_data_offset = data_off;
    wav_len         = data_size;
    wav_pos         = 0;
    wav_loaded      = 1;

#if SB_VERBOSE
    printf("sb_LoadWAV: streaming from offset=%lu size=%lu (%.1f sec)\n",
           data_off, data_size,
           (float)data_size / (float)(wav_rate * wav_channels)); fflush(stdout);
#endif

    return SB_OK;
}

int sb_Play(void) {
    unsigned char tc;

    if (!sb_ready) {
#if SB_VERBOSE
        printf("sb_Play: not ready\n"); fflush(stdout);
#endif
        return SB_ERR_NORESET;
    }
    if (!wav_loaded) {
#if SB_VERBOSE
        printf("sb_Play: no WAV loaded\n"); fflush(stdout);
#endif
        return SB_ERR_NORESET;
    }

    wav_pos = 0;
    fseek(wav_file, (long)wav_data_offset, SEEK_SET);

    /* Pre-fill both halves */
    fill_half(0);
    fill_half(1);

    /* Time constant: 256 - (1000000 / (channels * rate)) */
    tc = (unsigned char)(256 - (1000000UL / ((unsigned long)wav_channels * wav_rate)));

#if SB_VERBOSE
    printf("sb_Play: tc=%d rate=%lu ch=%d\n", (int)tc, wav_rate, wav_channels); fflush(stdout);
#endif

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

    /* DSP: speaker on, time constant, block size, start auto-init */
    dsp_write(DSP_SPEAKER_ON);
    dsp_write(DSP_SET_RATE);
    dsp_write(tc);
    dsp_write(DSP_SET_BLOCKSIZE);
    dsp_write((unsigned char)((HALF_SIZE - 1) & 0xFF));
    dsp_write((unsigned char)((HALF_SIZE - 1) >> 8));
    dsp_write(DSP_DMA_8BIT_AI);

    current_half = 0;
    playing = 1;

#if SB_VERBOSE
    printf("sb_Play: playing\n"); fflush(stdout);
#endif

    return SB_OK;
}

void sb_Stop(void) {
    if (!playing) return;
#if SB_VERBOSE
    printf("sb_Stop\n"); fflush(stdout);
#endif
    dsp_write(DSP_HALT_DMA);
    dsp_write(DSP_SPEAKER_OFF);
    playing = 0;
    if (wav_file != NULL) {
        fclose(wav_file);
        wav_file   = NULL;
        wav_loaded = 0;
    }
}

void sb_Tick(void) {
    int            should_be_half;
    int            idle_half;
    static int     tick_count = 0;
    static int     fill_count = 0;

    if (!playing || !sb_ready || !wav_loaded)
        return;

    tick_count++;

    /* Read the current DMA byte counter to determine which half is playing.
       The 8237 counts DOWN from (BUF_SIZE-1) to 0, then wraps.
       When pos >= HALF_SIZE the DMA is in the second half (1), otherwise first (0). */
    outportb(DMA_FLIP_FLOP, 0x00);
    unsigned int lo    = inportb(dma_count_reg[sb_dma]);
    unsigned int hi    = inportb(dma_count_reg[sb_dma]);
    unsigned int count = lo | (hi << 8);
    unsigned int pos   = (BUF_SIZE - 1) - count;

    should_be_half = (pos >= HALF_SIZE) ? 1 : 0;
    idle_half      = 1 - should_be_half;

#if SB_VERBOSE
    if (tick_count % 500 == 0) {
        printf("sb_Tick: count=%d fills=%d half=%d pos=%u\n",
               tick_count, fill_count, current_half, pos); fflush(stdout);
    }
#endif

    if (should_be_half != current_half) {
        fill_count++;
#if SB_VERBOSE
        printf("sb_Tick: crossing to half %d (fill #%d)\n",
               should_be_half, fill_count); fflush(stdout);
#endif
        inportb(sb_port + SB_ACK_8BIT);
        fill_half(idle_half);
        current_half = should_be_half;
    }
}

void sb_Shutdown(void) {
#if SB_VERBOSE
    printf("sb_Shutdown\n"); fflush(stdout);
#endif
    sb_Stop();
    if (wav_file != NULL) {
        fclose(wav_file);
        wav_file   = NULL;
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

void sb_SetVolume(int vol) {
    if (vol < 0)   vol = 0;
    if (vol > 256) vol = 256;
    sb_volume = vol;
}