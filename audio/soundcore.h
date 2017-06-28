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
 */

#ifndef _SOUNDCORE_H_
#define _SOUNDCORE_H_

#define AUDIO_STATUS_PLAYING    0x00000001
#define AUDIO_STATUS_NOTPLAYING 0x08000000

// AppleWin-sourced default error increment and max adjustment values ...
#define SOUNDCORE_ERROR_INC 20
#define SOUNDCORE_ERROR_MAX 200

// Note to future self:
//
// Although output for the speaker could be MONO (and was at one point in time) ... we optimize the OpenSLES backend on
// Android to have just one buffer queue callback, where we need to mix both mockingboard and speaker samples.
//
// For now, just make everything use stereo for simplicity (including OpenAL backend).
#define NUM_CHANNELS 2

typedef struct AudioBuffer_s {
    bool bActive;       // Mockingboard ... refactor?
    bool bMute;         // Mockingboard ... refactor?
    long nVolume;       // Mockingboard ... refactor?
    PRIVATE void *_internal;

    // Get current number of queued bytes
    long (*GetCurrentPosition)(struct AudioBuffer_s *_this, OUTPARM unsigned long *bytes_queued);

    // This method obtains a valid write pointer to the sound buffer's audio data
    long (*Lock)(struct AudioBuffer_s *_this, unsigned long write_bytes, INOUT int16_t **audio_ptr, OUTPARM unsigned long *audio_bytes);

    // This method releases a locked sound buffer.
    long (*Unlock)(struct AudioBuffer_s *_this, unsigned long audio_bytes);

    // Get status (playing or not)
    long (*GetStatus)(struct AudioBuffer_s *_this, OUTPARM unsigned long *status);

    // Mockingboard-specific buffer replay
    //long (*UnlockStaticBuffer)(struct AudioBuffer_s *_this, unsigned long audio_bytes);
    //long (*Replay)(struct AudioBuffer_s *_this);

} AudioBuffer_s;

/*
 * Creates a sound buffer object.
 */
long audio_createSoundBuffer(INOUT AudioBuffer_s **audioBuffer);

/*
 * Destroy and nullify sound buffer object.
 */
void audio_destroySoundBuffer(INOUT AudioBuffer_s **pVoice);

/*
 * Prepare the audio subsystem, including the backend renderer.
 */
bool audio_init(void);

/*
 * Shutdown the audio subsystem and backend renderer.
 */
void audio_shutdown(void);

/*
 * Pause the audio subsystem.
 */
void audio_pause(void);

/*
 * Resume the audio subsystem.
 */
void audio_resume(void);

/*
 * Set audio buffer latency
 */
void audio_setLatency(float latencySecs);

/*
 * Get audio buffer latency
 */
float audio_getLatency(void);

/*
 * Is the audio subsystem available?
 */
extern READONLY bool audio_isAvailable;

typedef struct AudioSettings_s {

    /*
     * Native device sample rate
     */
    READONLY unsigned long sampleRateHz;

    /*
     * Native device bytes-per-sample (currently assuming 16bit/2byte samples)
     */
    READONLY unsigned long bytesPerSample;

    /*
     * Native mono min/ideal buffer size in samples
     */
    READONLY unsigned long monoBufferSizeSamples;

    /*
     * Native stereo min/ideal buffer size in samples
     */
    READONLY unsigned long stereoBufferSizeSamples;
} AudioSettings_s;

// ----------------------------------------------------------------------------
// Private audio backend APIs

typedef struct AudioContext_s {
    PRIVATE void *_internal;
    PRIVATE long (*CreateSoundBuffer)(const struct AudioContext_s *sound_system, INOUT AudioBuffer_s **buffer);
    PRIVATE long (*DestroySoundBuffer)(const struct AudioContext_s *sound_system, INOUT AudioBuffer_s **buffer);
} AudioContext_s;

typedef struct AudioBackend_s {

    AudioSettings_s systemSettings;

    // basic backend functionality controlled by soundcore
    PRIVATE long (*setup)(INOUT AudioContext_s **audio_context);
    PRIVATE long (*shutdown)(INOUT AudioContext_s **audio_context);

    PRIVATE long (*pause)(AudioContext_s *audio_context);
    PRIVATE long (*resume)(AudioContext_s *audio_context);

} AudioBackend_s;

// Audio backend registered at CTOR time
extern AudioBackend_s *audio_backend;

#endif /* whole file */
