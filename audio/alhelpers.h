#ifndef ALHELPERS_H
#define ALHELPERS_H

#ifdef __APPLE__
#   import <OpenAL/al.h>
#   import <OpenAL/alc.h>
#else
#   include <AL/al.h>
#   include <AL/alc.h>
#   include <AL/alext.h>
#endif

#ifndef _WIN32
#include <unistd.h>
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Easy device init/deinit functions. */
ALCcontext* InitAL(void);
void CloseAL(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* ALHELPERS_H */
