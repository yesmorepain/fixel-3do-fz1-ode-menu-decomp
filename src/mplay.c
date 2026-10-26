/* mplay.c - reconstructed from mplay 0x2fc-0x4a8 (the only non-library code in mplay).
 * Loops assets/bgm44.aifc with the music.lib SoundFilePlayer. The file name ":mplay.c" is known from __FILE__;
 * function and variable names are guesses. The DIAGNOSE_SYSERR call must stay on line 136 (__LINE__ = 0x88);
 * the original's lines before it are unknown, so the gap is padded.
 */
#include "types.h"
#include "operror.h"
#include "audio.h"
#include "music.h"
#include "debug3do.h"
#include "stdio.h"

int32 play_sound_file_loop(char *name)
{
    int32 Result;
    SoundFilePlayer *sfp;
    int32 unused[2];        /* never used; 8 B of the 0xc-byte frame (type and name are guesses) */
    int32 unused1 = 0;      /* never used (r9) */
    int32 unused2 = 0;      /* never used (r8) */
    int32 SignalsIn = 0;
    int32 SignalsNeeded = 0;

    sfp = OpenSoundFile(name, 4, 0x10000);
    if (!sfp)
        goto error;
    printf("Sfp created\n");
    for (;;) {
        Result = StartSoundFile(sfp, 0x7fff);
        if (Result < 0)
            goto stop;
        do {
            Result = ServiceSoundFile(sfp, SignalsIn, &SignalsNeeded);
            if (SignalsNeeded)
                SignalsIn = WaitSignal(SignalsNeeded);
        } while (SignalsNeeded);
        Result = RewindSoundFile(sfp);
    }
stop:
    Result = StopSoundFile(sfp);
    Result = UnloadSoundFile(sfp);
error:
    PrintfSysErr(Result);
    return Result;
}

int main(int argc, char **argv)
{
    int32 Result;

    if ((Result = OpenAudioFolio()) < 0) {





















































































        DIAGNOSE_SYSERR(Result, ("Can't open the audio folio\n"));
    } else
        play_sound_file_loop("assets/bgm44.aifc");
    CloseAudioFolio();
    return 0;
}
