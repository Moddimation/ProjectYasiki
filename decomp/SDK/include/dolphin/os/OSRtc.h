#ifndef _DOLPHIN_OSRTC_H_
#define _DOLPHIN_OSRTC_H_

#include <types.h>

#ifdef __cplusplus
extern "C"
{
#endif

// make the assert happy
#define OS_SOUND_MODE_MONO   0
#define OS_SOUND_MODE_STEREO 1

// make the asserts happy
#define OS_VIDEO_MODE_NTSC   0
#define OS_VIDEO_MODE_MPAL   2
typedef struct OSSram
{
    u16 checkSum;
    u16 checkSumInv;
    u32 ead0;
    u32 ead1;
    u32 counterBias;
    s8  displayOffsetH;
    u8  ntd;
    u8  language;
    u8  flags;
} OSSram;
typedef struct OSSramEx
{
    u8  flashID[2][12];
    u32 wirelessKeyboardID;
    u16 wirelessPadID[4];
    u8  dvdErrorCode;
    u8  _padding0;
    u8  flashIDCheckSum[2];
    u8  _padding1[4];
} OSSramEx;
u32  OSGetSoundMode();
void OSSetSoundMode(u32 mode);
u32  OSGetVideoMode();
void OSSetVideoMode(u32 mode);
u16  OSGetLanguage();
void OSSetLanguage(u16 language);

#ifdef __cplusplus
}
#endif

#endif // _DOLPHIN_OSRTC_H_
