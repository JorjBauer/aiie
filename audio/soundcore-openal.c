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

// soundcore OpenAL backend -- streaming audio

#include "common.h"

#ifdef __APPLE__
#   import <OpenAL/al.h>
#   import <OpenAL/alc.h>
#else
#   include <AL/al.h>
#   include <AL/alc.h>
#   include <AL/alext.h>
#endif

#include "audio/alhelpers.h"
#include "playqueue.h"
#include "uthash.h"

#define DEBUG_OPENAL 0
#if DEBUG_OPENAL
#   define OPENAL_LOG(...) LOG(__VA_ARGS__)
#else
#   define OPENAL_LOG(...)
#endif

#define OPENAL_NUM_BUFFERS 4

typedef struct ALVoice {
    ALuint source;
    ALuint buffers[OPENAL_NUM_BUFFERS];

    // playing data
    PlayQueue_s *playq;
    ALint _queued_total_bytes; // a maximum estimate -- actual value depends on OpenAL query

    // working data buffer
    ALbyte *data;
    ALsizei index;      // working buffer byte index
    ALsizei buffersize; // working buffer size (and OpenAL buffersize)

    ALsizei replay_index;

    // sample parameters
    ALenum format;
    ALenum channels;
    ALenum type;
    ALuint rate;
} ALVoice;

typedef struct ALVoices {
    const ALuint source;
    ALVoice *voice;
    UT_hash_handle hh;
} ALVoices;

static ALVoices *voices = NULL;
static AudioBackend_s openal_audio_backend = { { 0 } };

// ----------------------------------------------------------------------------
// AudioBuffer_s processing routines

static void _playq_removeNode(ALVoice *voice, PlayNode_s *playNode) {
    long err = voice->playq->Remove(voice->playq, playNode);
    assert(err == 0);
    voice->_queued_total_bytes -= playNode->numBytes;
    assert(voice->_queued_total_bytes >= 0);
}

static long _ALProcessPlayBuffers(ALVoice *voice, ALuint *bytes_queued) {
    long err = 0;
    *bytes_queued = 0;

    do {
        ALint processed = 0;
        alGetSourcei(voice->source, AL_BUFFERS_PROCESSED, &processed);
        if ((err = alGetError()) != AL_NO_ERROR) {
            ERRLOG("OOPS, error in checking processed buffers : 0x%08lx", err);
            break;
        }

        while (processed > 0) {
            --processed;
            ALuint bufid = 0;
            alSourceUnqueueBuffers(voice->source, 1, &bufid);
            if ((err = alGetError()) != AL_NO_ERROR) {
                ERRLOG("OOPS, OpenAL error dequeuing buffer : 0x%08lx", err);
                break;
            }

            OPENAL_LOG("Attempting to dequeue %u ...", bufid);
            PlayNode_s playNode = {
                .nodeId = bufid,
            };
            err = voice->playq->Get(voice->playq, &playNode);
            if (err) {
                ERRLOG("OOPS, OpenAL bufid %u not found in playlist...", bufid);
            } else {
                _playq_removeNode(voice, &playNode);
            }
        }

        ALint play_offset = 0;
        alGetSourcei(voice->source, AL_BYTE_OFFSET, &play_offset);
        if ((err = alGetError()) != AL_NO_ERROR) {
            ERRLOG("OOPS, alGetSourcei AL_BYTE_OFFSET : 0x%08lx", err);
            break;
        }
        assert((play_offset >= 0)/* && (play_offset < voice->buffersize)*/);

        long q = voice->_queued_total_bytes/* + voice->index*/ - play_offset;

        if (q >= 0) {
            *bytes_queued = (ALuint)q;
        }
    } while (0);

    return err;
}

// returns queued+working sound buffer size in bytes
static long ALGetPosition(AudioBuffer_s *_this, OUTPARM unsigned long *bytes_queued) {
    *bytes_queued = 0;
    long err = 0;

    do {
        ALVoice *voice = (ALVoice*)_this->_internal;

        ALuint queued = 0;
        long err = _ALProcessPlayBuffers(voice, &queued);
        if (err) {
            break;
        }
#if DEBUG_OPENAL
        static int last_queued = 0;
        if (queued != last_queued) {
            last_queued = queued;
            OPENAL_LOG("OpenAL bytes queued : %u", queued);
        }
#endif

        *bytes_queued = queued + voice->index;
    } while (0);

    return err;
}

static long ALLockBuffer(AudioBuffer_s *_this, unsigned long write_bytes, INOUT int16_t **audio_ptr, OUTPARM unsigned long *audio_bytes) {
    *audio_bytes = 0;
    *audio_ptr = NULL;
    long err = 0;

    do {
        ALVoice *voice = (ALVoice*)_this->_internal;

        if (write_bytes == 0) {
            write_bytes = voice->buffersize;
        }

        ALuint bytes_queued = 0;
        err = _ALProcessPlayBuffers(voice, &bytes_queued);
        if (err) {
            break;
        }

        if ((bytes_queued == 0) && (voice->index == 0)) {
            LOG("Buffer underrun ... queuing quiet samples ...");
            int quiet_size = voice->buffersize>>2/* 1/4 buffer */;
            memset(voice->data, 0x0, quiet_size);
            voice->index += quiet_size;
        }
#if 0
        else if (bytes_queued + voice->index < (voice->buffersize>>3)/* 1/8 buffer */)
        {
            OPENAL_LOG("Potential underrun ...");
        }
#endif

        ALsizei remaining = voice->buffersize - voice->index;
        if (write_bytes > remaining) {
            write_bytes = remaining;
        }

        *audio_ptr = (int16_t *)(voice->data+voice->index);
        *audio_bytes = write_bytes;
    } while (0);

    return err;
}

static long _ALSubmitBufferToOpenAL(ALVoice *voice) {
    long err = 0;

    do {
        // Micro-manage play queue locally to understand the total bytes-in-play
        PlayNode_s playNode = {
            .nodeId = INVALID_NODE_ID,
            .numBytes = voice->index,
            .bytes = (uint8_t *)(voice->data),
        };
        err = voice->playq->Enqueue(voice->playq, &playNode);
        if (err) {
            break;
        }
        voice->_queued_total_bytes += voice->index;
        voice->index = 0;
        assert(voice->_queued_total_bytes > 0);

        OPENAL_LOG("Enqueing OpenAL buffer %ld (%lu bytes) at %p", playNode.nodeId, playNode.numBytes, playNode.bytes);
        alBufferData(playNode.nodeId, voice->format, playNode.bytes, playNode.numBytes, voice->rate);
        if ((err = alGetError()) != AL_NO_ERROR) {
            _playq_removeNode(voice, &playNode);
            ERRLOG("OOPS, Error alBufferData : 0x%08lx", err);
            break;
        }

        ALuint nodeId = (ALuint)playNode.nodeId;
        alSourceQueueBuffers(voice->source, 1, &nodeId);
        if ((err = alGetError()) != AL_NO_ERROR) {
            _playq_removeNode(voice, &playNode);
            ERRLOG("OOPS, Error buffering data : 0x%08lx", err);
            break;
        }

        ALint state = 0;
        alGetSourcei(voice->source, AL_SOURCE_STATE, &state);
        if ((err = alGetError()) != AL_NO_ERROR) {
            ERRLOG("OOPS, Error checking source state : 0x%08lx", err);
            break;
        }
        if ((state != AL_PLAYING) && (state != AL_PAUSED)) {
            // 2013/11/17 NOTE : alSourcePlay() is expensive and causes audio artifacts, only invoke if needed
            LOG("Restarting playback (was 0x%08x) ...", state);
            alSourcePlay(voice->source);
            if ((err = alGetError()) != AL_NO_ERROR) {
                LOG("Error starting playback : 0x%08lx", err);
                break;
            }
        }
    } while (0);

    return err;
}

static long ALUnlockBuffer(AudioBuffer_s *_this, unsigned long audio_bytes) {
    long err = 0;

    do {
        ALVoice *voice = (ALVoice*)_this->_internal;
        ALuint bytes_queued = 0;
        err = _ALProcessPlayBuffers(voice, &bytes_queued);
        if (err) {
            break;
        }

        voice->index += audio_bytes;


        if (voice->index >= voice->buffersize) {
            assert((voice->index == voice->buffersize) && "OOPS, detected an actual overflow in queued sound data");
        }

        if (bytes_queued >= (voice->buffersize>>2)/*quarter buffersize*/) {
            // keep accumulating data into working buffer
            break;
        }

        if (! (voice->playq->CanEnqueue(voice->playq)) ) {
            OPENAL_LOG("no free audio buffers"); // keep accumulating ...
            break;
        }

        // Submit working buffer to OpenAL

        err = _ALSubmitBufferToOpenAL(voice);
    } while (0);

    return err;
}

#if 0
// HACK Part I : done once for mockingboard that has semiauto repeating phonemes ...
static long ALUnlockStaticBuffer(AudioBuffer_s *_this, unsigned long audio_bytes) {
    ALVoice *voice = (ALVoice*)_this->_internal;
    voice->replay_index = (ALsizei)audio_bytes;
    return 0;
}

// HACK Part II : replay mockingboard phoneme ...
static long ALReplay(AudioBuffer_s *_this) {
    ALVoice *voice = (ALVoice*)_this->_internal;
    voice->index = voice->replay_index;
    long err = _ALSubmitBufferToOpenAL(voice);
    return err;
}
#endif

static long ALGetStatus(AudioBuffer_s *_this, OUTPARM unsigned long *status) {
    *status = -1;
    long err = 0;

    do {
        ALVoice* voice = (ALVoice*)_this->_internal;
        ALint state = 0;
        alGetSourcei(voice->source, AL_SOURCE_STATE, &state);
        if ((err = alGetError()) != AL_NO_ERROR) {
            ERRLOG("OOPS, Error checking source state : 0x%08lx", err);
            break;
        }

        if ((state == AL_PLAYING) || (state == AL_PAUSED)) {
            *status = AUDIO_STATUS_PLAYING;
        } else {
            *status = AUDIO_STATUS_NOTPLAYING;
        }
    } while (0);

    return err;
}

// ----------------------------------------------------------------------------
// ALVoice is the AudioBuffer_s->_internal

static void _openal_destroyVoice(ALVoice *voice) {
    alDeleteSources(1, &voice->source);
    if (alGetError() != AL_NO_ERROR) {
        ERRLOG("OOPS, Failed to delete source");
    }

    if (voice->data) {
        FREE(voice->data);
    }

    for (unsigned int i=0; i<OPENAL_NUM_BUFFERS; i++) {
        alDeleteBuffers(1, voice->buffers);
        if (alGetError() != AL_NO_ERROR) {
            ERRLOG("OOPS, Failed to delete object IDs");
        }
    }

    playq_destroyPlayQueue(&(voice->playq));

    memset(voice, 0, sizeof(*voice));
    FREE(voice);
}

static ALVoice *_openal_createVoice(unsigned long numChannels) {
    ALVoice *voice = NULL;

    do {
        voice = CALLOC(1, sizeof(*voice));
        if (voice == NULL) {
            ERRLOG("OOPS, Out of memory!");
            break;
        }

        alGenBuffers(OPENAL_NUM_BUFFERS, voice->buffers);
        if (alGetError() != AL_NO_ERROR) {
            ERRLOG("OOPS, Could not create buffers");
            break;
        }

        alGenSources(1, &voice->source);
        if (alGetError() != AL_NO_ERROR) {
            ERRLOG("OOPS, Could not create source");
            break;
        }

        // Set parameters so mono sources play out the front-center speaker and won't distance attenuate.
        alSource3i(voice->source, AL_POSITION, 0, 0, -1);
        if (alGetError() != AL_NO_ERROR) {
            ERRLOG("OOPS, Could not set AL_POSITION source parameter");
            break;
        }
        alSourcei(voice->source, AL_SOURCE_RELATIVE, AL_TRUE);
        if (alGetError() != AL_NO_ERROR) {
            ERRLOG("OOPS, Could not set AL_SOURCE_RELATIVE source parameter");
            break;
        }
        alSourcei(voice->source, AL_ROLLOFF_FACTOR, 0);
        if (alGetError() != AL_NO_ERROR) {
            ERRLOG("OOPS, Could not set AL_ROLLOFF_FACTOR source parameter");
            break;
        }

#if 0
        alSourcei(voice->source, AL_STREAMING, AL_TRUE);
        if (alGetError() != AL_NO_ERROR) {
            ERRLOG("OOPS, Could not set AL_STREAMING source parameter");
            break;
        }
#endif

        long longBuffers[OPENAL_NUM_BUFFERS];
        for (unsigned int i=0; i<OPENAL_NUM_BUFFERS; i++) {
            longBuffers[i] = (long)(voice->buffers[i]);
        }
        voice->playq = playq_createPlayQueue(longBuffers, OPENAL_NUM_BUFFERS);
        if (!voice->playq) {
            ERRLOG("OOPS, Not enough memory for PlayQueue");
            break;
        }

        voice->rate = openal_audio_backend.systemSettings.sampleRateHz;

        // Emulator supports only mono and stereo
        if (numChannels == 2) {
            voice->format = AL_FORMAT_STEREO16;
        } else {
            voice->format = AL_FORMAT_MONO16;
        }

        /* Allocate enough space for the temp buffer, given the format */
        assert(numChannels == 1 || numChannels == 2);
        unsigned long maxSamples = openal_audio_backend.systemSettings.monoBufferSizeSamples * numChannels;
        voice->buffersize = maxSamples * openal_audio_backend.systemSettings.bytesPerSample;

        voice->data = CALLOC(1, voice->buffersize);
        if (voice->data == NULL) {
            ERRLOG("OOPS, Error allocating %d bytes", voice->buffersize);
            break;
        }

        LOG("\tRate     : 0x%08x", voice->rate);
        LOG("\tFormat   : 0x%08x", voice->format);
        LOG("\tbuffersize : %d", voice->buffersize);

        return voice;

    } while(0);

    // ERR
    if (voice) {
        _openal_destroyVoice(voice);
    }

    return NULL;
}

// ----------------------------------------------------------------------------

static long openal_destroySoundBuffer(const struct AudioContext_s *sound_system, INOUT AudioBuffer_s **soundbuf_struct) {
    if (!*soundbuf_struct) {
        // already dealloced
        return 0;
    }

    LOG("openal_destroySoundBuffer ...");
    ALVoice *voice = (ALVoice *)((*soundbuf_struct)->_internal);
    ALint source = voice->source;

    _openal_destroyVoice(voice);

    ALVoices *vnode = NULL;
    HASH_FIND_INT(voices, &source, vnode);
    if (vnode) {
        HASH_DEL(voices, vnode);
        FREE(vnode);
    }

    FREE(*soundbuf_struct);
    return 0;
}

static long openal_createSoundBuffer(const AudioContext_s *audio_context, INOUT AudioBuffer_s **soundbuf_struct) {
    LOG("openal_createSoundBuffer ...");
    assert(*soundbuf_struct == NULL);

    ALVoice *voice = NULL;

    do {

        ALCcontext *ctx = (ALCcontext*)(audio_context->_internal);
        assert(ctx != NULL);

        if ((voice = _openal_createVoice(NUM_CHANNELS)) == NULL) {
            ERRLOG("OOPS, Cannot create new voice");
            break;
        }

        ALVoices immutableNode = { /*const*/.source = voice->source };
        ALVoices *vnode = CALLOC(1, sizeof(ALVoices));
        if (!vnode) {
            ERRLOG("OOPS, Not enough memory");
            break;
        }
        memcpy(vnode, &immutableNode, sizeof(ALVoices));
        vnode->voice = voice;
        HASH_ADD_INT(voices, source, vnode);

        if ((*soundbuf_struct = CALLOC(1, sizeof(AudioBuffer_s))) == NULL) {
            ERRLOG("OOPS, Not enough memory");
            break;
        }

        (*soundbuf_struct)->_internal          = voice;
        (*soundbuf_struct)->GetCurrentPosition = &ALGetPosition;
        (*soundbuf_struct)->Lock               = &ALLockBuffer;
        (*soundbuf_struct)->Unlock             = &ALUnlockBuffer;
        (*soundbuf_struct)->GetStatus          = &ALGetStatus;
        // mockingboard-specific hacks
        //(*soundbuf_struct)->UnlockStaticBuffer = &ALUnlockStaticBuffer;
        //(*soundbuf_struct)->Replay             = &ALReplay;

        return 0;
    } while(0);

    if (*soundbuf_struct) {
        openal_destroySoundBuffer(audio_context, soundbuf_struct);
    } else if (voice) {
        _openal_destroyVoice(voice);
    }

    return -1;
}

// ----------------------------------------------------------------------------

static long openal_systemShutdown(INOUT AudioContext_s **audio_context) {
    assert(*audio_context != NULL);

    ALCcontext *ctx = (ALCcontext*) (*audio_context)->_internal;
    assert(ctx != NULL);
    (*audio_context)->_internal = NULL;
    FREE(*audio_context);

    // NOTE : currently assuming just one OpenAL global context
    CloseAL();

    return 0;
}

static long openal_systemSetup(INOUT AudioContext_s **audio_context) {
    assert(*audio_context == NULL);
    assert(voices == NULL);

    long result = -1;
    ALCcontext *ctx = NULL;

    // 2015/06/29 these values seem to work well on Linux desktop ... no other OpenAL platform has been tested
    openal_audio_backend.systemSettings.sampleRateHz = 22050;
    openal_audio_backend.systemSettings.bytesPerSample = 2;
    openal_audio_backend.systemSettings.monoBufferSizeSamples = (8*1024);
    openal_audio_backend.systemSettings.stereoBufferSizeSamples = openal_audio_backend.systemSettings.monoBufferSizeSamples;

    do {

        if ((ctx = InitAL()) == NULL) {
            // NOTE : currently assuming just one OpenAL global context
            ERRLOG("OOPS, OpenAL initialize failed");
            break;
        }

        if (alIsExtensionPresent("AL_SOFT_buffer_samples")) {
            LOG("AL_SOFT_buffer_samples supported, good!");
        } else {
            LOG("WARNING - AL_SOFT_buffer_samples extension not supported... Proceeding anyway...");
        }

        if ((*audio_context = CALLOC(1, sizeof(AudioContext_s))) == NULL) {
            ERRLOG("OOPS, Not enough memory");
            break;
        }

        (*audio_context)->_internal = ctx;
        (*audio_context)->CreateSoundBuffer = &openal_createSoundBuffer;
        (*audio_context)->DestroySoundBuffer = &openal_destroySoundBuffer;

        result = 0;
    } while(0);

    if (result) {
        if (ctx) {
            AudioContext_s *ctxPtr = CALLOC(1, sizeof(AudioContext_s));
            ctxPtr->_internal = ctx;
            openal_systemShutdown(&ctxPtr);
        }
        assert (*audio_context == NULL);
        LOG("OpenAL sound output disabled");
    }

    return result;
}

static long openal_systemPause(AudioContext_s *audio_context) {
    ALVoices *vnode = NULL;
    ALVoices *tmp = NULL;
    long err = 0;

    HASH_ITER(hh, voices, vnode, tmp) {
        alSourcePause(vnode->source);
        err = alGetError();
        if (err != AL_NO_ERROR) {
            ERRLOG("OOPS, Failed to pause source : 0x%08lx", err);
        }
    }

    return 0;
}

static long openal_systemResume(AudioContext_s *audio_context) {
    ALVoices *vnode = NULL;
    ALVoices *tmp = NULL;
    long err = 0;

    HASH_ITER(hh, voices, vnode, tmp) {
        alSourcePlay(vnode->source);
        err = alGetError();
        if (err != AL_NO_ERROR) {
            ERRLOG("OOPS, Failed to pause source : 0x%08lx", err);
        }
    }

    return 0;
}

static void _init_openal(void) {
    LOG("Initializing OpenAL sound system");

    assert((audio_backend == NULL) && "there can only be one!");

    openal_audio_backend.setup            = &openal_systemSetup;
    openal_audio_backend.shutdown         = &openal_systemShutdown;
    openal_audio_backend.pause            = &openal_systemPause;
    openal_audio_backend.resume           = &openal_systemResume;

    audio_backend = &openal_audio_backend;
}

static __attribute__((constructor)) void __init_openal(void) {
    emulator_registerStartupCallback(CTOR_PRIORITY_EARLY, &_init_openal);
}

