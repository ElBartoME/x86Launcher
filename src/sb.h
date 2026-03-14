/* sb.h - Sound Blaster 8-bit DMA streaming WAV playback for x86Launcher. */

#ifndef __HAS_SB
#define __HAS_SB

#define SB_OK            0
#define SB_ERR_NOBLASTER -1
#define SB_ERR_NORESET   -2
#define SB_ERR_FILE      -3
#define SB_ERR_BADWAV    -4
#define SB_ERR_MEM       -5
#define SB_ERR_NODMA     -6

int  sb_Init(void);
int  sb_LoadWAV(const char *path);
int  sb_Play(void);
void sb_Stop(void);
void sb_Tick(void);
void sb_Shutdown(void);
int  sb_IsPlaying(void);

#endif