/*
 * Apple // emulator for *ix
 *
 * This software package is subject to the GNU General Public License
 * version 3 or later (your choice) as published by the Free Software
 * Foundation.
 *
 * Copyright 2013-2015 Aaron Culliney
 *
 */

/*
 * Apple //e core sound system support. Source inspired/derived from AppleWin.
 *
 * 2015/10/01 AUDIO LIFECYCLE WARNING : CPU thread owns all audio, otherwise bad things may happen in system sound layer
 * (OpenAL/OpenSLES/ALSA/etc)
 */

#include "common.h"

#define MAX_SOUND_DEVICES 100

static AudioContext_s *audioContext = NULL;

bool audio_isAvailable = false;
float audio_latencySecs = 0.25f;
AudioBackend_s *audio_backend = NULL;

//-----------------------------------------------------------------------------

long audio_createSoundBuffer(INOUT AudioBuffer_s **audioBuffer) {
    // CPU thread owns audio lifecycle (see note above)
    assert(pthread_self() == cpu_thread_id);

    if (!audio_isAvailable) {
        *audioBuffer = NULL;
        return -1;
    }

    if (*audioBuffer) {
        audio_destroySoundBuffer(audioBuffer);
    }

    long err = 0;
    do {
        if (!audioContext) {
            ERRLOG("Cannot create sound buffer, no context");
            err = -1;
            break;
        }
        err = audioContext->CreateSoundBuffer(audioContext, audioBuffer);
        if (err) {
            break;
        }
    } while (0);

    return err;
}

void audio_destroySoundBuffer(INOUT AudioBuffer_s **audioBuffer) {
    // CPU thread owns audio lifecycle (see note above)
    assert(pthread_self() == cpu_thread_id);
    if (audioContext) {
        audioContext->DestroySoundBuffer(audioContext, audioBuffer);
    }
}

bool audio_init(void) {
    // CPU thread owns audio lifecycle (see note above)
    assert(pthread_self() == cpu_thread_id);
    if (audio_isAvailable) {
        return true;
    }

    do {
        if (!audio_backend) {
            LOG("No backend audio available, cannot initialize soundcore");
            break;
        }

        if (audioContext) {
            audio_backend->shutdown(&audioContext);
        }

        long err = audio_backend->setup((AudioContext_s**)&audioContext);
        if (err) {
            LOG("Failed to create an audio context!");
            break;
        }

        audio_isAvailable = true;
    } while (0);

    return audio_isAvailable;
}

void audio_shutdown(void) {
    // CPU thread owns audio lifecycle (see note above)
    assert(pthread_self() == cpu_thread_id);
    if (!audio_isAvailable) {
        return;
    }
    audio_backend->shutdown(&audioContext);
    audio_isAvailable = false;
}

void audio_pause(void) {
    // CPU thread owns audio lifecycle (see note above)
    // Deadlock on Kindle Fire 1st Gen if audio_pause() and audio_resume() happen off CPU thread ...
#ifdef __APPLE__
#   warning FIXME TODO : this assert is firing on iOS port ... but the assert is valid ... fix soon 
#else
    assert(pthread_self() == cpu_thread_id);
#endif
    if (!audio_isAvailable) {
        return;
    }
    audio_backend->pause(audioContext);
}

void audio_resume(void) {
    // CPU thread owns audio lifecycle (see note above)
    assert(pthread_self() == cpu_thread_id);
    if (!audio_isAvailable) {
        return;
    }
    audio_backend->resume(audioContext);
}

void audio_setLatency(float latencySecs) {
    audio_latencySecs = latencySecs;
}

float audio_getLatency(void) {
    return audio_latencySecs;
}

