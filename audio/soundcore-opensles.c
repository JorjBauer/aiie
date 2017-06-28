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

// soundcore OpenSLES backend -- streaming audio

#include "common.h"

#include <SLES/OpenSLES.h>
#if defined(ANDROID)
#   include <SLES/OpenSLES_Android.h>
#else
#   error FIXME TODO this currently uses Android BufferQueue extensions...
#endif

#define DEBUG_OPENSL 0
#if DEBUG_OPENSL
#   define OPENSL_LOG(...) LOG(__VA_ARGS__)
#else
#   define OPENSL_LOG(...)
#endif

#define NUM_CHANNELS 2

typedef struct SLVoice {
    void *ctx; // EngineContext_s

    // working data buffer
    uint8_t *ringBuffer;            // ringBuffer of total size : bufferSize+submitSize
    unsigned long bufferSize;       // ringBuffer non-overflow size
    ptrdiff_t writeHead;            // head of the writer of ringBuffer (speaker, mockingboard)
    unsigned long writeWrapCount;   // count of buffer wraps for the writer

    unsigned long spinLock;         // spinlock around reader variables
    ptrdiff_t readHead;             // head of the reader of ringBuffer (OpenSLES callback)
    unsigned long readWrapCount;    // count of buffer wraps for the reader

    // next voice
    struct SLVoice *next;
} SLVoice;

typedef struct EngineContext_s {
    SLObjectItf engineObject;
    SLEngineItf engineEngine;
    SLObjectItf outputMixObject;

    SLObjectItf bqPlayerObject;
    SLPlayItf bqPlayerPlay;
    SLAndroidSimpleBufferQueueItf bqPlayerBufferQueue;

    uint8_t *mixBuf;                // mix buffer submitted to OpenSLES
    unsigned long submitSize;       // buffer size OpenSLES expects/wants

    SLVoice *voices;
    SLVoice *recycledVoices;

} EngineContext_s;

static AudioBackend_s opensles_audio_backend = { 0 };

// ----------------------------------------------------------------------------
// AudioBuffer_s internal processing routines

// Check and resets underrun condition (readHead has advanced beyond writeHead)
static inline bool _underrun_check_and_manage(SLVoice *voice, OUTPARM unsigned long *workingBytes) {

    SPINLOCK_ACQUIRE(&voice->spinLock);
    unsigned long readHead = voice->readHead;
    unsigned long readWrapCount = voice->readWrapCount;
    SPINLOCK_RELINQUISH(&voice->spinLock);

    assert(readHead < voice->bufferSize);
    assert(voice->writeHead < voice->bufferSize);

    bool isUnder = false;
    if ( (readWrapCount > voice->writeWrapCount) ||
            ((readHead >= voice->writeHead) && (readWrapCount == voice->writeWrapCount)) )
    {
        isUnder = true;
        LOG("Buffer underrun ...");
        voice->writeHead = readHead;
        voice->writeWrapCount = readWrapCount;
    }

    if (readHead <= voice->writeHead) {
        *workingBytes = voice->writeHead - readHead;
    } else {
        *workingBytes = voice->writeHead + (voice->bufferSize - readHead);
    }

    return isUnder;
}

// This callback handler is called presumably every time (or just prior to when) a buffer finishes playing and the
// system needs moar data (of the correct buffer size)
static void bqPlayerCallback(SLAndroidSimpleBufferQueueItf bq, void *context) {

    // invariant : can always read submitSize from position of readHead (bufferSize+submitSize)

    EngineContext_s *ctx = (EngineContext_s *)context;

    SLresult result = SL_RESULT_SUCCESS;
    do {
        // This is a very simple inline mixer so that we only need one BufferQueue (which works best on low-end Android
        // devices
        //
        // HACK ASSUMPTIONS :
        //  * max of 2 voices/buffers
        //  * both buffers contain stereo signed 16bit samples with zero as mid point
        //  * absolute value of maximum amplitude is less than one half SHRT_MAX (to avoid clipping)
        SLVoice *voice0 = ctx->voices;
        if (!voice0) {
            result = -1;
            break;
        }

        // copy/mix data

        memcpy(ctx->mixBuf, voice0->ringBuffer+voice0->readHead, ctx->submitSize);

        SLVoice *voice1 = voice0->next;
        if (voice1) {

            // add second waveform into mix buffer
            ////if (SIMD_IS_AVAILABLE()) {
#if USE_SIMD
#warning FIXME TODO vectorial code here
#endif
            ////} else {
                uint16_t *mixBuf = (uint16_t *)ctx->mixBuf;
                unsigned long submitSize = ctx->submitSize>>1;
                for (unsigned long i=0; i<submitSize; i++) {
                    mixBuf[i] += ((uint16_t *)(voice1->ringBuffer+voice1->readHead))[i];
                }
            ////}
        }

        // submit data to OpenSLES

        result = (*bq)->Enqueue(bq, ctx->mixBuf, ctx->submitSize);

        // now manage quiet backfilling and overflow/wrapping ...

        memset(voice0->ringBuffer+voice0->readHead, 0x0, ctx->submitSize); // backfill quiet samples

        unsigned long newReadHead0 = voice0->readHead + ctx->submitSize;
        unsigned long newReadWrapCount0 = voice0->readWrapCount;

        if (newReadHead0 >= voice0->bufferSize) {
            newReadHead0 = newReadHead0 - voice0->bufferSize;
            memset(voice0->ringBuffer+voice0->bufferSize, 0x0, ctx->submitSize); // backfill quiet samples
            memset(voice0->ringBuffer, 0x0, newReadHead0);
            ++newReadWrapCount0;
        }

        SPINLOCK_ACQUIRE(&voice0->spinLock);
        voice0->readHead = newReadHead0;
        voice0->readWrapCount = newReadWrapCount0;
        SPINLOCK_RELINQUISH(&voice0->spinLock);

        if (voice1) {
            memset(voice1->ringBuffer+voice1->readHead, 0x0, ctx->submitSize); // backfill quiet samples

            unsigned long newReadHead1 = voice1->readHead + ctx->submitSize;
            unsigned long newReadWrapCount1 = voice1->readWrapCount;

            if (newReadHead1 >= voice1->bufferSize) {
                newReadHead1 = newReadHead1 - voice1->bufferSize;
                memset(voice1->ringBuffer+voice1->bufferSize, 0x0, ctx->submitSize); // backfill quiet samples
                memset(voice1->ringBuffer, 0x0, newReadHead1);
                ++newReadWrapCount1;
            }

            SPINLOCK_ACQUIRE(&voice1->spinLock);
            voice1->readHead = newReadHead1;
            voice1->readWrapCount = newReadWrapCount1;
            SPINLOCK_RELINQUISH(&voice1->spinLock);
        }

    } while (0);

    if (result != SL_RESULT_SUCCESS) {
        LOG("WARNING: could not enqueue data to OpenSLES!");
        (*(ctx->bqPlayerPlay))->SetPlayState(ctx->bqPlayerPlay, SL_PLAYSTATE_STOPPED);
    }
}

static long _SLMaybeSubmitAndStart(SLVoice *voice) {
    SLuint32 state = 0;
    EngineContext_s *ctx = (EngineContext_s *)voice->ctx;
    SLresult result = (*(ctx->bqPlayerPlay))->GetPlayState(ctx->bqPlayerPlay, &state);
    if (result != SL_RESULT_SUCCESS) {
        ERRLOG("OOPS, could not get source state : %lu", (unsigned long)result);
    } else {
        if ((state != SL_PLAYSTATE_PLAYING) && (state != SL_PLAYSTATE_PAUSED)) {
            LOG("FORCING restart audio buffer queue playback ...");
            result = (*(ctx->bqPlayerPlay))->SetPlayState(ctx->bqPlayerPlay, SL_PLAYSTATE_PLAYING);
            bqPlayerCallback(ctx->bqPlayerBufferQueue, ctx);
        }
    }
    return result;
}

// ----------------------------------------------------------------------------
// AudioBuffer_s public API handlers

// returns queued+working sound buffer size in bytes
static long SLGetPosition(AudioBuffer_s *_this, OUTPARM unsigned long *bytes_queued) {
    *bytes_queued = 0;
    long err = 0;

    do {
        SLVoice *voice = (SLVoice*)_this->_internal;

        unsigned long workingBytes = 0;
        bool underrun = _underrun_check_and_manage(voice, &workingBytes);
        //bool overrun = _overrun_check_and_manage(voice);

        unsigned long queuedBytes = 0;
        if (!underrun) {
            //queuedBytes = ctx->submitSize; // assume that there are always about this much actually queued
        }

        assert(workingBytes <= voice->bufferSize);
        *bytes_queued = workingBytes;
    } while (0);

    return err;
}

static long SLLockBuffer(AudioBuffer_s *_this, unsigned long write_bytes, INOUT int16_t **audio_ptr, OUTPARM unsigned long *audio_bytes) {
    *audio_bytes = 0;
    *audio_ptr = NULL;
    long err = 0;

    //OPENSL_LOG("SLLockBuffer request for %ld bytes", write_bytes);

    do {
        SLVoice *voice = (SLVoice*)_this->_internal;
        EngineContext_s *ctx = (EngineContext_s *)voice->ctx;

        if (write_bytes == 0) {
            LOG("HMMM ... writing full buffer!");
            write_bytes = voice->bufferSize;
        }

        unsigned long workingBytes = 0;
        _underrun_check_and_manage(voice, &workingBytes);
        unsigned long availableBytes = voice->bufferSize - workingBytes;

        assert(workingBytes <= voice->bufferSize);
        assert(voice->writeHead < voice->bufferSize);

        // TODO FIXME : maybe need to resurrect the 2 inner pointers and foist the responsibility onto the
        // speaker/mockingboard modules so we can actually write moar here?
        unsigned long writableBytes = MIN( availableBytes, ((voice->bufferSize+ctx->submitSize) - voice->writeHead) );

        if (write_bytes > writableBytes) {
            OPENSL_LOG("NOTE truncating audio buffer (call again to write complete requested buffer) ...");
            write_bytes = writableBytes;
        }

        *audio_ptr = (int16_t *)(voice->ringBuffer+voice->writeHead);
        *audio_bytes = write_bytes;
    } while (0);

    return err;
}

static long SLUnlockBuffer(AudioBuffer_s *_this, unsigned long audio_bytes) {
    long err = 0;

    do {
        SLVoice *voice = (SLVoice*)_this->_internal;
        EngineContext_s *ctx = (EngineContext_s *)voice->ctx;

        unsigned long previousWriteHead = voice->writeHead;

        voice->writeHead += audio_bytes;

        assert((voice->writeHead <= (voice->bufferSize + ctx->submitSize)) && "OOPS, real overflow in queued sound data!");

        if (voice->writeHead >= voice->bufferSize) {
            // copy data from overflow into beginning of buffer
            voice->writeHead = voice->writeHead - voice->bufferSize;
            ++voice->writeWrapCount;
            memcpy(voice->ringBuffer, voice->ringBuffer+voice->bufferSize, voice->writeHead);
        } else if (previousWriteHead < ctx->submitSize) {
            // copy data in beginning of buffer into overflow position
            unsigned long copyNumBytes = MIN(audio_bytes, ctx->submitSize-previousWriteHead);
            memcpy(voice->ringBuffer+voice->bufferSize+previousWriteHead, voice->ringBuffer+previousWriteHead, copyNumBytes);
        }

        err = _SLMaybeSubmitAndStart(voice);
    } while (0);

    return err;
}

#if 0
// HACK Part I : done once for mockingboard that has semiauto repeating phonemes ...
static long SLUnlockStaticBuffer(AudioBuffer_s *_this, unsigned long audio_bytes) {
    SLVoice *voice = (SLVoice*)_this->_internal;
    voice->replay_index = audio_bytes;
    return 0;
}

// HACK Part II : replay mockingboard phoneme ...
static long SLReplay(AudioBuffer_s *_this) {
    SLVoice *voice = (SLVoice*)_this->_internal;

    SPINLOCK_ACQUIRE(&voice->spinLock);
    voice->readHead = 0;
    voice->writeHead = voice->replay_index;
    SPINLOCK_RELINQUISH(&voice->spinLock);

    long err = _SLMaybeSubmitAndStart(voice);
#warning FIXME TODO ... how do we handle mockingboard for new OpenSLES buffer queue codepath?
    return err;
}
#endif

static long SLGetStatus(AudioBuffer_s *_this, OUTPARM unsigned long *status) {
    *status = -1;
    SLresult result = SL_RESULT_UNKNOWN_ERROR;

    do {
        SLVoice* voice = (SLVoice*)_this->_internal;
        EngineContext_s *ctx = (EngineContext_s *)voice->ctx;

        SLuint32 state = 0;
        result = (*(ctx->bqPlayerPlay))->GetPlayState(ctx->bqPlayerPlay, &state);
        if (result != SL_RESULT_SUCCESS) {
            ERRLOG("OOPS, could not get source state : %lu", (unsigned long)result);
            break;
        }

        if ((state == SL_PLAYSTATE_PLAYING) || (state == SL_PLAYSTATE_PAUSED)) {
            *status = AUDIO_STATUS_PLAYING;
        } else {
            *status = AUDIO_STATUS_NOTPLAYING;
        }
    } while (0);

    return (long)result;
}

// ----------------------------------------------------------------------------
// SLVoice is the AudioBuffer_s->_internal

static inline void _opensl_destroyVoice(SLVoice *voice) {
    if (voice->ringBuffer) {
        FREE(voice->ringBuffer);
    }
    memset(voice, 0, sizeof(*voice));
    FREE(voice);
}

static long opensl_destroySoundBuffer(const struct AudioContext_s *audio_context, INOUT AudioBuffer_s **soundbuf_struct) {
    if (!*soundbuf_struct) {
        return 0;
    }

    LOG("opensl_destroySoundBuffer ...");

    EngineContext_s *ctx = (EngineContext_s *)(audio_context->_internal);

    SLVoice *v = (SLVoice *)((*soundbuf_struct)->_internal);

    SLVoice *vprev = NULL;
    SLVoice *voice = ctx->voices;
    while (voice) {
        if (voice == v) {
            if (vprev) {
                vprev->next = voice->next;
            } else {
                ctx->voices = voice->next;
            }
            break;
        }
        vprev = voice;
        voice = voice->next;
    }

    assert(voice && "voice should exist, or speaker, mockingboard, etc are not using this internal API correctly!");

    // Do not actually destory the voice here since we could race with the buffer queue.  purge these on complete sound
    // system shutdown

    voice->next = ctx->recycledVoices;
    ctx->recycledVoices = voice;

    memset(*soundbuf_struct, 0x0, sizeof(soundbuf_struct));
    FREE(*soundbuf_struct);

    return 0;
}

static long opensl_createSoundBuffer(const AudioContext_s *audio_context, INOUT AudioBuffer_s **soundbuf_struct) {
    LOG("opensl_createSoundBuffer ...");
    assert(*soundbuf_struct == NULL);

    SLVoice *voice = NULL;

    do {

        EngineContext_s *ctx = (EngineContext_s *)(audio_context->_internal);
        assert(ctx != NULL);

        unsigned long bufferSize = opensles_audio_backend.systemSettings.stereoBufferSizeSamples * opensles_audio_backend.systemSettings.bytesPerSample * NUM_CHANNELS;

        if (ctx->recycledVoices) {
            LOG("Recycling previous SLVoice ...");
            voice = ctx->recycledVoices;
            ctx->recycledVoices = voice->next;
            uint8_t *prevBuffer = voice->ringBuffer;
            memset(voice, 0x0, sizeof(*voice));
            voice->bufferSize = bufferSize;
            voice->ringBuffer = prevBuffer;
        } else {
            LOG("Creating new SLVoice ...");
            voice = CALLOC(1, sizeof(*voice));
            if (voice == NULL) {
                ERRLOG("OOPS, Out of memory!");
                break;
            }
            voice->bufferSize = bufferSize;
            // Allocate enough space for the temp buffer (including a maximum allowed overflow)
            voice->ringBuffer = CALLOC(1, voice->bufferSize + ctx->submitSize/*max overflow*/);
            if (voice->ringBuffer == NULL) {
                ERRLOG("OOPS, Error allocating %lu bytes", (unsigned long)voice->bufferSize+ctx->submitSize);
                break;
            }
        }

        LOG("ideal stereo submission bufsize is %lu (bytes:%lu)", (unsigned long)android_stereoBufferSubmitSizeSamples, (unsigned long)ctx->submitSize);

        voice->ctx = ctx;

        if ((*soundbuf_struct = CALLOC(1, sizeof(AudioBuffer_s))) == NULL) {
            ERRLOG("OOPS, Not enough memory");
            break;
        }

        (*soundbuf_struct)->_internal          = voice;
        (*soundbuf_struct)->GetCurrentPosition = &SLGetPosition;
        (*soundbuf_struct)->Lock               = &SLLockBuffer;
        (*soundbuf_struct)->Unlock             = &SLUnlockBuffer;
        (*soundbuf_struct)->GetStatus          = &SLGetStatus;
        // mockingboard-specific (SSI263) hacks
        //(*soundbuf_struct)->UnlockStaticBuffer = &SLUnlockStaticBuffer;
        //(*soundbuf_struct)->Replay             = &SLReplay;

        voice->next = ctx->voices;
        ctx->voices = voice;

        LOG("Successfully created SLVoice");

        return 0;
    } while(0);

    if (*soundbuf_struct) {
        opensl_destroySoundBuffer(audio_context, soundbuf_struct);
    } else if (voice) {
        _opensl_destroyVoice(voice);
    }

    return -1;
}

// ----------------------------------------------------------------------------

static long opensles_systemShutdown(AudioContext_s **audio_context) {
    assert(*audio_context != NULL);

    EngineContext_s *ctx = (EngineContext_s *)((*audio_context)->_internal);
    assert(ctx != NULL);

    // destroy buffer queue audio player object, and invalidate all associated interfaces
    if (ctx->bqPlayerObject != NULL) {
        (*(ctx->bqPlayerObject))->Destroy(ctx->bqPlayerObject);
        ctx->bqPlayerObject = NULL;
        ctx->bqPlayerPlay = NULL;
        ctx->bqPlayerBufferQueue = NULL;
    }

    // destroy output mix object, and invalidate all associated interfaces
    if (ctx->outputMixObject != NULL) {
        (*(ctx->outputMixObject))->Destroy(ctx->outputMixObject);
        ctx->outputMixObject = NULL;
    }

    // destroy engine object, and invalidate all associated interfaces
    if (ctx->engineObject != NULL) {
        (*(ctx->engineObject))->Destroy(ctx->engineObject);
        ctx->engineObject = NULL;
        ctx->engineEngine = NULL;
    }

    if (ctx->mixBuf) {
        FREE(ctx->mixBuf);
    }

    assert(ctx->voices == NULL && "incorrect API usage");

    SLVoice *voice = ctx->recycledVoices;
    while (voice) {
        SLVoice *vkill = voice;
        voice = voice->next;
        _opensl_destroyVoice(vkill);
    }

    memset(ctx, 0x0, sizeof(EngineContext_s));
    FREE(ctx);

    memset(*audio_context, 0x0, sizeof(AudioContext_s));
    FREE(*audio_context);

    return 0;
}

static long opensles_systemSetup(INOUT AudioContext_s **audio_context) {
    assert(*audio_context == NULL);

    EngineContext_s *ctx = NULL;
    SLresult result = -1;

    opensles_audio_backend.systemSettings.sampleRateHz = android_deviceSampleRateHz;
    opensles_audio_backend.systemSettings.bytesPerSample = 2;

    if (android_deviceSampleRateHz <= 22050/*sentinel in DevicePropertyCalculator.java*/) {
        android_stereoBufferSubmitSizeSamples >>= 1; // value from Android/Java DevicePropertyCalculator.java seems to be pre-multiplied by channel size?
    }

    opensles_audio_backend.systemSettings.monoBufferSizeSamples = android_deviceSampleRateHz * audio_getLatency();
    opensles_audio_backend.systemSettings.stereoBufferSizeSamples = android_deviceSampleRateHz * audio_getLatency();

    if (android_stereoBufferSubmitSizeSamples<<2 > opensles_audio_backend.systemSettings.stereoBufferSizeSamples) {
        opensles_audio_backend.systemSettings.stereoBufferSizeSamples = android_stereoBufferSubmitSizeSamples<<2;
        LOG("Changing stereo buffer size to be %lu samples", (unsigned long)opensles_audio_backend.systemSettings.stereoBufferSizeSamples);
    }
    if (android_monoBufferSubmitSizeSamples<<2 > opensles_audio_backend.systemSettings.monoBufferSizeSamples) {
        opensles_audio_backend.systemSettings.monoBufferSizeSamples = android_monoBufferSubmitSizeSamples<<2;
        LOG("Changing mono buffer size to be %lu samples", (unsigned long)opensles_audio_backend.systemSettings.monoBufferSizeSamples);
    }
#warning TODO FIXME ^^^^^ need a dynamic bufferSize calculation/calibration routine to determine optimal buffer size for device ... may also need a user-initiated calibration too

    do {
        //
        // Engine creation ...
        //
        ctx = CALLOC(1, sizeof(EngineContext_s));
        if (!ctx) {
            result = -1;
            break;
        }

        ctx->submitSize = android_stereoBufferSubmitSizeSamples * opensles_audio_backend.systemSettings.bytesPerSample * NUM_CHANNELS;
        ctx->mixBuf = CALLOC(1, ctx->submitSize);
        if (ctx->mixBuf == NULL) {
            ERRLOG("OOPS, Error allocating %lu bytes", (unsigned long)ctx->submitSize);
            break;
        }

        // create basic engine
        result = slCreateEngine(&(ctx->engineObject), 0, NULL, /*engineMixIIDCount:*/0, /*engineMixIIDs:*/NULL, /*engineMixReqs:*/NULL);
        if (result != SL_RESULT_SUCCESS) {
            ERRLOG("Could not create OpenSLES Engine : %lu", (unsigned long)result);
            break;
        }

        // realize the engine
        result = (*(ctx->engineObject))->Realize(ctx->engineObject, /*asynchronous_realization:*/SL_BOOLEAN_FALSE);
        if (result != SL_RESULT_SUCCESS) {
            ERRLOG("Could not realize the OpenSLES Engine : %lu", (unsigned long)result);
            break;
        }

        // get the actual engine interface
        result = (*(ctx->engineObject))->GetInterface(ctx->engineObject, SL_IID_ENGINE, &(ctx->engineEngine));
        if (result != SL_RESULT_SUCCESS) {
            ERRLOG("Could not get the OpenSLES Engine : %lu", (unsigned long)result);
            break;
        }

        //
        // Output Mix ...
        //

        result = (*(ctx->engineEngine))->CreateOutputMix(ctx->engineEngine, &(ctx->outputMixObject), 0, NULL, NULL);
        if (result != SL_RESULT_SUCCESS) {
            ERRLOG("Could not create output mix : %lu", (unsigned long)result);
            break;
        }

        // realize the output mix
        result = (*(ctx->outputMixObject))->Realize(ctx->outputMixObject, SL_BOOLEAN_FALSE);
        if (result != SL_RESULT_SUCCESS) {
            ERRLOG("Could not realize the output mix : %lu", (unsigned long)result);
            break;
        }

        // create soundcore API wrapper
        if ((*audio_context = CALLOC(1, sizeof(AudioContext_s))) == NULL) {
            result = -1;
            ERRLOG("OOPS, Not enough memory");
            break;
        }

        //
        // OpenSLES buffer queue player setup
        //

        // configure audio source
        SLDataLocator_AndroidSimpleBufferQueue loc_bufq = {
            .locatorType = SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE,
            .numBuffers = 2,
#warning FIXME TODO ... verify 2 numBuffers is best
        };
        SLDataFormat_PCM format_pcm = {
            .formatType = SL_DATAFORMAT_PCM,
            .numChannels = 2,
            .samplesPerSec = opensles_audio_backend.systemSettings.sampleRateHz * 1000,
            .bitsPerSample = SL_PCMSAMPLEFORMAT_FIXED_16,
            .containerSize = SL_PCMSAMPLEFORMAT_FIXED_16,
            .channelMask = SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT,
            .endianness = SL_BYTEORDER_LITTLEENDIAN,
        };
        SLDataSource audioSrc = {
            .pLocator = &loc_bufq,
            .pFormat = &format_pcm,
        };

        // configure audio sink
        SLDataLocator_OutputMix loc_outmix = {
            .locatorType = SL_DATALOCATOR_OUTPUTMIX,
            .outputMix = ctx->outputMixObject,
        };
        SLDataSink audioSnk = {
            .pLocator = &loc_outmix,
            .pFormat = NULL,
        };

        // create audio player
#define _NUM_INTERFACES 3
        const SLInterfaceID ids[_NUM_INTERFACES] = {
            SL_IID_BUFFERQUEUE,
            SL_IID_EFFECTSEND,
            //SL_IID_MUTESOLO,
            SL_IID_VOLUME,
        };
        const SLboolean req[_NUM_INTERFACES] = {
            SL_BOOLEAN_TRUE,
            SL_BOOLEAN_TRUE,
            //numChannels == 1 ? SL_BOOLEAN_FALSE : SL_BOOLEAN_TRUE,
            SL_BOOLEAN_TRUE,
        };

        result = (*(ctx->engineEngine))->CreateAudioPlayer(ctx->engineEngine, &(ctx->bqPlayerObject), &audioSrc, &audioSnk, _NUM_INTERFACES, ids, req);
        if (result != SL_RESULT_SUCCESS) {
            ERRLOG("OOPS, could not create the BufferQueue player object : %lu", (unsigned long)result);
            break;
        }

        // realize the player
        result = (*(ctx->bqPlayerObject))->Realize(ctx->bqPlayerObject, /*asynchronous_realization:*/SL_BOOLEAN_FALSE);
        if (result != SL_RESULT_SUCCESS) {
            ERRLOG("OOPS, could not realize the BufferQueue player object : %lu", (unsigned long)result);
            break;
        }

        // get the play interface
        result = (*(ctx->bqPlayerObject))->GetInterface(ctx->bqPlayerObject, SL_IID_PLAY, &(ctx->bqPlayerPlay));
        if (result != SL_RESULT_SUCCESS) {
            ERRLOG("OOPS, could not get the play interface : %lu", (unsigned long)result);
            break;
        }

        // get the buffer queue interface
        result = (*(ctx->bqPlayerObject))->GetInterface(ctx->bqPlayerObject, SL_IID_BUFFERQUEUE, &(ctx->bqPlayerBufferQueue));
        if (result != SL_RESULT_SUCCESS) {
            ERRLOG("OOPS, could not get the BufferQueue play interface : %lu", (unsigned long)result);
            break;
        }

        // register callback on the buffer queue
        result = (*(ctx->bqPlayerBufferQueue))->RegisterCallback(ctx->bqPlayerBufferQueue, bqPlayerCallback, ctx);
        if (result != SL_RESULT_SUCCESS) {
            ERRLOG("OOPS, could not register BufferQueue callback : %lu", (unsigned long)result);
            break;
        }

        (*audio_context)->_internal = ctx;
        (*audio_context)->CreateSoundBuffer = &opensl_createSoundBuffer;
        (*audio_context)->DestroySoundBuffer = &opensl_destroySoundBuffer;

        LOG("Successfully created OpenSLES engine and buffer queue");

    } while (0);

    if (result != SL_RESULT_SUCCESS) {
        if (ctx) {
            AudioContext_s *ctxPtr = CALLOC(1, sizeof(AudioContext_s));
            ctxPtr->_internal = ctx;
            opensles_systemShutdown(&ctxPtr);
        }
        assert(*audio_context == NULL);
        LOG("OpenSLES sound output disabled");
    }

    return result;
}

// pause all audio
static long opensles_systemPause(AudioContext_s *audio_context) {
    LOG("OpenSLES pausing play");

    EngineContext_s *ctx = (EngineContext_s *)(audio_context->_internal);
    SLresult result = (*(ctx->bqPlayerPlay))->SetPlayState(ctx->bqPlayerPlay, SL_PLAYSTATE_PAUSED);

    return 0;
}

static long opensles_systemResume(AudioContext_s *audio_context) {
    LOG("OpenSLES resuming play");

    SLuint32 state = 0;
    EngineContext_s *ctx = (EngineContext_s *)(audio_context->_internal);
    SLresult result = (*(ctx->bqPlayerPlay))->GetPlayState(ctx->bqPlayerPlay, &state);

    do {
        if (result != SL_RESULT_SUCCESS) {
            ERRLOG("OOPS, could not get source state when attempting to resume : %lu", (unsigned long)result);
            break;
        }

        if (state != SL_PLAYSTATE_PLAYING) {
            ERRLOG("WARNING: possible audio lifecycle mismatch ... continuing anyway");
        }

        if (state == SL_PLAYSTATE_PAUSED) {
            // Balanced resume OK here
            SLresult result = (*(ctx->bqPlayerPlay))->SetPlayState(ctx->bqPlayerPlay, SL_PLAYSTATE_PLAYING);
        } else if (state == SL_PLAYSTATE_STOPPED) {
            // Do not resume for stopped state, let this get forced from CPU/speaker thread otherwise we starve. (The
            // stopped state happens if user dynamically changed buffer parameters in menu settings which triggered an
            // OpenSLES destroy/re-initialization ... e.g. audio_setLatency() was invoked)
        }
    } while (0);

    return result;
}

static void _init_opensl(void) {
    LOG("Initializing OpenSLES sound system");

    assert(audio_backend == NULL && "there can only be one!");

    opensles_audio_backend.setup            = &opensles_systemSetup;
    opensles_audio_backend.shutdown         = &opensles_systemShutdown;
    opensles_audio_backend.pause            = &opensles_systemPause;
    opensles_audio_backend.resume           = &opensles_systemResume;

    audio_backend = &opensles_audio_backend;
}

static __attribute__((constructor)) void __init_opensl(void) {
    emulator_registerStartupCallback(CTOR_PRIORITY_EARLY, &_init_opensl);
}

