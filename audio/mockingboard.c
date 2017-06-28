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
AppleWin : An Apple //e emulator for Windows

Copyright (C) 1994-1996, Michael O'Brien
Copyright (C) 1999-2001, Oliver Schmidt
Copyright (C) 2002-2005, Tom Charlesworth
Copyright (C) 2006-2007, Tom Charlesworth, Michael Pohoreski

AppleWin is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.

AppleWin is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with AppleWin; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/* Description: Mockingboard/Phasor emulation
 *
 * Author: Copyright (c) 2002-2006, Tom Charlesworth
 */

// History:
//
// v1.12.07.1 (30 Dec 2005)
// - Update 6522 TIMERs after every 6502 opcode, giving more precise IRQs
// - Minimum TIMER freq is now 0x100 cycles
// - Added Phasor support
//
// v1.12.06.1 (16 July 2005)
// - Reworked 6522's ORB -> AY8910 decoder
// - Changed MB output so L=All voices from AY0 & AY2 & R=All voices from AY1 & AY3
// - Added crude support for Votrax speech chip (by using SSI263 phonemes)
//
// v1.12.04.1 (14 Sep 2004)
// - Switch MB output from dual-mono to stereo.
// - Relaxed TIMER1 freq from ~62Hz (period=0x4000) to ~83Hz (period=0x3000).
//
// 25 Apr 2004:
// - Added basic support for the SSI263 speech chip
//
// 15 Mar 2004:
// - Switched to MAME's AY8910 emulation (includes envelope support)
//
// v1.12.03 (11 Jan 2004)
// - For free-running 6522 timer1 IRQ, reload with current ACCESS_TIMER1 value.
//   (Fixes Ultima 4/5 playback speed problem.)
//
// v1.12.01 (24 Nov 2002)
// - Shaped the tone waveform more logarithmically
// - Added support for MB ena/dis switch on Config dialog
// - Added log file support
//
// v1.12.00 (17 Nov 2002)
// - Initial version (no AY8910 envelope support)
//

// Notes on Votrax chip (on original Mockingboards):
// From Crimewave (Penguin Software):
// . Init:
//   . DDRB = 0xFF
//   . PCR  = 0xB0
//   . IER  = 0x90
//   . ORB  = 0x03 (PAUSE0) or 0x3F (STOP)
// . IRQ:
//   . ORB  = Phoneme value
// . IRQ last phoneme complete:
//   . IER  = 0x10
//   . ORB  = 0x3F (STOP)
//

#if 0 // !APPLE2IX
#include "StdAfx.h"

#include "SaveState_Structs_v1.h"

#include "AppleWin.h"
#include "CPU.h"
#include "Log.h"
#include "Memory.h"
#include "Mockingboard.h"
#include "SoundCore.h"
#include "YamlHelper.h"

#include "AY8910.h"
#include "SSI263Phonemes.h"
#else

#define DSBCAPS_LOCSOFTWARE         0x00000008
#define DSBCAPS_CTRLVOLUME          0x00000080
#define DSBCAPS_CTRLPOSITIONNOTIFY  0x00000100

#define DSBVOLUME_MIN               -10000
#define DSBVOLUME_MAX               0

#include "common.h"
#       if defined(__linux) && !defined(ANDROID)
#       include <sys/io.h>
#       endif
#       if TESTING
#       include "greatest.h"
#       endif

#if defined(FAILED)
#undef FAILED
#endif
static inline bool FAILED(int x) { return x != 0; }

#define THREAD_PRIORITY_NORMAL 0
#define THREAD_PRIORITY_TIME_CRITICAL 15
#define STILL_ACTIVE 259

#include <wchar.h>
#include "audio/AY8910.h"
#include "audio/SSI263Phonemes.h"

#define g_bFullSpeed is_fullspeed
#define g_bDisableDirectSound false
#endif

#define LOG_SSI263 0


#define SY6522_DEVICE_A 0
#define SY6522_DEVICE_B 1

#define SLOT4 4
#define SLOT5 5

#define NUM_MB 2
#define NUM_DEVS_PER_MB 2
#define NUM_AY8910 (NUM_MB*NUM_DEVS_PER_MB)
#define NUM_SY6522 NUM_AY8910
#define NUM_VOICES_PER_AY8910 3
#define NUM_VOICES (NUM_AY8910*NUM_VOICES_PER_AY8910)


// Chip offsets from card base.
#define SY6522A_Offset	0x00
#define SY6522B_Offset	0x80
#define SSI263_Offset	0x40

#define Phasor_SY6522A_CS		4
#define Phasor_SY6522B_CS		7
#define Phasor_SY6522A_Offset	(1<<Phasor_SY6522A_CS)
#define Phasor_SY6522B_Offset	(1<<Phasor_SY6522B_CS)

typedef struct
{
	SY6522 sy6522;
	uint8_t nAY8910Number;
	uint8_t nAYCurrentRegister;
	uint8_t nTimerStatus;
	SSI263A SpeechChip;
} SY6522_AY8910;


// IFR & IER:
#define IxR_PERIPHERAL	(1<<1)
#define IxR_VOTRAX		(1<<4)	// TO DO: Get proper name from 6522 datasheet!
#define IxR_TIMER2		(1<<5)
#define IxR_TIMER1		(1<<6)

// ACR:
#define RUNMODE		(1<<6)	// 0 = 1-Shot Mode, 1 = Free Running Mode
#define RM_ONESHOT		(0<<6)
#define RM_FREERUNNING	(1<<6)


// SSI263A registers:
#define SSI_DURPHON	0x00
#define SSI_INFLECT	0x01
#define SSI_RATEINF	0x02
#define SSI_CTTRAMP	0x03
#define SSI_FILFREQ	0x04


// Support 2 MB's, each with 2x SY6522/AY8910 pairs.
static SY6522_AY8910 g_MB[NUM_AY8910];

// Timer vars
static unsigned long g_n6522TimerPeriod = 0;
#define TIMERDEVICE_INVALID -1
static unsigned int g_nMBTimerDevice = TIMERDEVICE_INVALID;	// SY6522 device# which is generating timer IRQ
static unsigned long g_uLastCumulativeCycles = 0;

// SSI263 vars:
static uint16_t g_nSSI263Device = 0;	// SSI263 device# which is generating phoneme-complete IRQ
static volatile int g_nCurrentActivePhoneme = -1;	// Modified by threads: main & SSI263Thread
static volatile bool g_bStopPhoneme = false;		// Modified by threads: main & SSI263Thread
static bool g_bVotraxPhoneme = false;

#if 1 // APPLE2IX
static unsigned long SAMPLE_RATE = 0;
static float samplesScale = 1.f;
#else
static const DWORD SAMPLE_RATE = 44100;	// Use a base freq so that DirectX (or sound h/w) doesn't have to up/down-sample
#endif

static short* ppAYVoiceBuffer[NUM_VOICES] = {0};

#if 1 // APPLE2IX
bool g_bDisableDirectSoundMockingboard = false;
static unsigned long g_nMB_InActiveCycleCount = 0;
#else
static unsigned __int64	g_nMB_InActiveCycleCount = 0;
#endif
static bool g_bMB_RegAccessedFlag = false;
static bool g_bMB_Active = false;

#if 1 // APPLE2IX
static pthread_t g_hThread = 0;
#else
static HANDLE g_hThread = NULL;
#endif

static bool g_bMBAvailable = false;

//

static SS_CARDTYPE g_SoundcardType = CT_Empty;	// Use CT_Empty to mean: no soundcard
static bool g_bPhasorEnable = false;
static uint8_t g_nPhasorMode = 0;	// 0=Mockingboard emulation, 1=Phasor native
static unsigned int g_PhasorClockScaleFactor = 1;	// for save-state only

//-------------------------------------

static const unsigned short g_nMB_NumChannels = 2;

#if 1 // APPLE2IX
static unsigned long g_dwDSBufferSize = 0;
#else
static const DWORD g_dwDSBufferSize = MAX_SAMPLES * sizeof(short) * g_nMB_NumChannels;
#endif

static const int16_t nWaveDataMin = (int16_t)0x8000;
static const int16_t nWaveDataMax = (int16_t)0x7FFF;

#if 1 // APPLE2IX
static short *g_nMixBuffer = NULL;
#else
static short g_nMixBuffer[g_dwDSBufferSize / sizeof(short)];
#endif


#if 1 // APPLE2IX
#   if MB_TRACING
static FILE *mb_trace_fp = NULL;
static FILE *mb_trace_samples_fp = NULL;
static unsigned long cycles_mb_toggled_r = 0;
static unsigned long cycles_mb_toggled_w = 0;
#   endif

static AudioBuffer_s *MockingboardVoice = NULL;
static AudioBuffer_s *SSI263Voice[64] = { 0 };
static pthread_cond_t ssi263_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t ssi263_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint8_t quit_event = false;
#else
static VOICE MockingboardVoice = {0};
static VOICE SSI263Voice[64] = {0};

static const int g_nNumEvents = 2;
static HANDLE g_hSSI263Event[g_nNumEvents] = {NULL};	// 1: Phoneme finished playing, 2: Exit thread
static DWORD g_dwMaxPhonemeLen = 0;
#endif

// When 6522 IRQ is *not* active use 60Hz update freq for MB voices
static const double g_f6522TimerPeriod_NoIRQ = CLK_6502 / 60.0;		// Constant whatever the CLK is set to

//---------------------------------------------------------------------------

// External global vars:
bool g_bMBTimerIrqActive = false;
#if 0 // _DEBUG
uint32_t g_uTimer1IrqCount = 0;	// DEBUG
#endif

//---------------------------------------------------------------------------

// Forward refs:
#if 0 // !APPLE2IX
static DWORD WINAPI SSI263Thread(LPVOID);
static void Votrax_Write(BYTE nDevice, BYTE nValue);
#else
#   if 0 // ENABLE_SSI263
static void* SSI263Thread(void *);
static void Votrax_Write(uint8_t nDevice, uint8_t nValue);
#   else
static void mb_assert(bool condition) {
#       ifdef NDEBUG
    // RELEASE
#       else
    // DEBUG
    assert(condition);
#       endif
}
#   endif
#endif

//---------------------------------------------------------------------------

static void StartTimer(SY6522_AY8910* pMB)
{
	if((pMB->nAY8910Number & 1) != SY6522_DEVICE_A)
		return;

	if((pMB->sy6522.IER & IxR_TIMER1) == 0x00)
		return;

	uint16_t nPeriod = pMB->sy6522.TIMER1_LATCH.w;

//	if(nPeriod <= 0xff)		// Timer1L value has been written (but TIMER1H hasn't)
//		return;

	pMB->nTimerStatus = 1;

	// 6522 CLK runs at same speed as 6502 CLK
	g_n6522TimerPeriod = nPeriod;

	g_bMBTimerIrqActive = true;
	g_nMBTimerDevice = pMB->nAY8910Number;
}

//-----------------------------------------------------------------------------

static void StopTimer(SY6522_AY8910* pMB)
{
	pMB->nTimerStatus = 0;
	g_bMBTimerIrqActive = false;
	g_nMBTimerDevice = TIMERDEVICE_INVALID;
}

//-----------------------------------------------------------------------------

static void ResetSY6522(SY6522_AY8910* pMB)
{
	memset(&pMB->sy6522,0,sizeof(SY6522));

	if(pMB->nTimerStatus)
		StopTimer(pMB);

	pMB->nAYCurrentRegister = 0;
}

//-----------------------------------------------------------------------------

static void AY8910_Write(uint8_t nDevice, uint8_t nReg, uint8_t nValue, uint8_t nAYDevice)
{
	g_bMB_RegAccessedFlag = true;
	SY6522_AY8910* pMB = &g_MB[nDevice];

	if((nValue & 4) == 0)
	{
		// RESET: Reset AY8910 only
		AY8910_reset(nDevice+2*nAYDevice);
#if MB_TRACING
                _mb_trace_AY8910(nDevice+2*nAYDevice, mb_trace_fp);
#endif
	}
	else
	{
		// Determine the AY8910 inputs
		int nBDIR = (nValue & 2) ? 1 : 0;
		const int nBC2 = 1;		// Hardwired to +5V
		int nBC1 = nValue & 1;

		int nAYFunc = (nBDIR<<2) | (nBC2<<1) | nBC1;
		enum {AY_NOP0, AY_NOP1, AY_INACTIVE, AY_READ, AY_NOP4, AY_NOP5, AY_WRITE, AY_LATCH};

		switch(nAYFunc)
		{
			case AY_INACTIVE:	// 4: INACTIVE
				break;

			case AY_READ:		// 5: READ FROM PSG (need to set DDRA to input)
				break;

			case AY_WRITE:		// 6: WRITE TO PSG
				_AYWriteReg(nDevice+2*nAYDevice, pMB->nAYCurrentRegister, pMB->sy6522.ORA
#if MB_TRACING
                                        , mb_trace_fp
#endif
                                        );
#if MB_TRACING
                                _mb_trace_AY8910(nDevice+2*nAYDevice, mb_trace_fp);
#endif
				break;

			case AY_LATCH:		// 7: LATCH ADDRESS
				// http://www.worldofspectrum.org/forums/showthread.php?t=23327
				// Selecting an unused register number above 0x0f puts the AY into a state where
				// any values written to the data/address bus are ignored, but can be read back
				// within a few tens of thousands of cycles before they decay to zero.
				if(pMB->sy6522.ORA <= 0x0F)
					pMB->nAYCurrentRegister = pMB->sy6522.ORA & 0x0F;
				// else Pro-Mockingboard (clone from HK)
				break;
#if 1 // APPLE2IX
                        default:
                                mb_assert(false);
#endif
		}
	}
}

static void UpdateIFR(SY6522_AY8910* pMB)
{
	pMB->sy6522.IFR &= 0x7F;

	if(pMB->sy6522.IFR & pMB->sy6522.IER & 0x7F)
		pMB->sy6522.IFR |= 0x80;

	// Now update the IRQ signal from all 6522s
	// . OR-sum of all active TIMER1, TIMER2 & SPEECH sources (from all 6522s)
	unsigned int bIRQ = 0;
	for(unsigned int i=0; i<NUM_SY6522; i++)
		bIRQ |= g_MB[i].sy6522.IFR & 0x80;

	// NB. Mockingboard generates IRQ on both 6522s:
	// . SSI263's IRQ (A/!R) is routed via the 2nd 6522 (at $Cx80) and must generate a 6502 IRQ (not NMI)
	// . SC-01's IRQ (A/!R) is also routed via a (2nd?) 6522
	// Phasor's SSI263 appears to be wired directly to the 6502's IRQ (ie. not via a 6522)
	// . I assume Phasor's 6522s just generate 6502 IRQs (not NMIs)

	if (bIRQ)
	{
#if 1 // APPLE2IX
#if MB_TRACING
            if (mb_trace_fp) {
                fprintf(mb_trace_fp, "\t%s\n", "IRQ_6522");
            }
#endif
            cpu65_interrupt(IS_6522);
#else
	    CpuIrqAssert(IS_6522);
#endif
	}
	else
	{
#if 1 // APPLE2IX
#if MB_TRACING
            if (mb_trace_fp) {
                fprintf(mb_trace_fp, "\t%s\n", "!IRQ_6522");
            }
#endif
            cpu65_uninterrupt(IS_6522);
#else
	    CpuIrqDeassert(IS_6522);
#endif
	}
}

#if MB_TRACING
static void _mb_traceWriteSample(INOUT char **tracingBufPtr, INOUT size_t *tracingBufSize, const char *samPrefix, int dat) {
    int sz = INT_MAX;

    char *buf = *tracingBufPtr;
    int max = (int)*tracingBufSize;

    sz = snprintf(buf, max, "%s[%08x]", samPrefix, dat);
    assert(sz > 0 && sz < max);
    buf += sz;
    max -= sz;
    assert(max >= 0);

    *tracingBufPtr = buf;
    *tracingBufSize = (size_t)max;
}

static void _mb_traceSY6522_AY8910(uint8_t nDevice) {
    SY6522_AY8910* pMB = &g_MB[nDevice];

    fprintf(mb_trace_fp, "\tSYS6522_AY8910(%d) nAY8910Number:%02X nAYCurrentRegister:%02X nTimerStatus:%02X\n", nDevice, pMB->nAY8910Number, pMB->nAYCurrentRegister, pMB->nTimerStatus);

    SY6522 *sy6522 = &pMB->sy6522;
    fprintf(mb_trace_fp, "\t\tSYS6522 : ORB:%02X ORA:%02X DDRB:%02X DDRA:%02X TIMER1_COUNTER:%04X TIMER1_LATCH:%04X TIMER2_COUNTER:%04X TIMER2_LATCH:%04X SERIAL_SHIFT:%02X ACR:%02X PCR:%02X IFR:%02X IER:%02X ORA_NO_HS:%02X\n", sy6522->ORB, sy6522->ORA, sy6522->DDRB, sy6522->DDRA, sy6522->TIMER1_COUNTER.w, sy6522->TIMER1_LATCH.w, sy6522->TIMER2_COUNTER.w, sy6522->TIMER2_LATCH.w, sy6522->SERIAL_SHIFT, sy6522->ACR, sy6522->PCR, sy6522->IFR, sy6522->IER, sy6522->ORA_NO_HS);

#if 0 // ENABLE_SSI263
    TODO FIXME : trace SSI263 stuff
#endif
}
#endif

static void SY6522_Write(uint8_t nDevice, uint8_t nReg, uint8_t nValue)
{
#if MB_TRACING
        if (mb_trace_fp) {
            fprintf(mb_trace_fp, "\tSY6522_Write(%02X, %02X, %02X)...\n", nDevice, nReg, nValue);
        }
#endif
	g_bMB_Active = true;

	SY6522_AY8910* pMB = &g_MB[nDevice];

	switch (nReg)
	{
		case 0x00:	// ORB
			{
				nValue &= pMB->sy6522.DDRB;
				pMB->sy6522.ORB = nValue;

				if( (pMB->sy6522.DDRB == 0xFF) && (pMB->sy6522.PCR == 0xB0) )
				{
					// Votrax speech data
#if 0 // ENABLE_SSI263
					Votrax_Write(nDevice, nValue);
#else
                                        mb_assert(false);
#endif
					break;
				}

				if(g_bPhasorEnable)
				{
					int nAY_CS = (g_nPhasorMode & 1) ? (~(nValue >> 3) & 3) : 1;

					if(nAY_CS & 1)
						AY8910_Write(nDevice, nReg, nValue, 0);

					if(nAY_CS & 2)
						AY8910_Write(nDevice, nReg, nValue, 1);
				}
				else
				{
					AY8910_Write(nDevice, nReg, nValue, 0);
				}

				break;
			}
		case 0x01:	// ORA
			pMB->sy6522.ORA = nValue & pMB->sy6522.DDRA;
			break;
		case 0x02:	// DDRB
			pMB->sy6522.DDRB = nValue;
			break;
		case 0x03:	// DDRA
			pMB->sy6522.DDRA = nValue;
			break;
		case 0x04:	// TIMER1L_COUNTER
		case 0x06:	// TIMER1L_LATCH
			pMB->sy6522.TIMER1_LATCH.l = nValue;
			break;
		case 0x05:	// TIMER1H_COUNTER
			/* Initiates timer1 & clears time-out of timer1 */

			// Clear Timer Interrupt Flag.
			pMB->sy6522.IFR &= ~IxR_TIMER1;
			UpdateIFR(pMB);

			pMB->sy6522.TIMER1_LATCH.h = nValue;
			pMB->sy6522.TIMER1_COUNTER.w = pMB->sy6522.TIMER1_LATCH.w;

			StartTimer(pMB);
			break;
		case 0x07:	// TIMER1H_LATCH
			// Clear Timer1 Interrupt Flag.
			pMB->sy6522.TIMER1_LATCH.h = nValue;
			pMB->sy6522.IFR &= ~IxR_TIMER1;
			UpdateIFR(pMB);
			break;
		case 0x08:	// TIMER2L
			pMB->sy6522.TIMER2_LATCH.l = nValue;
			break;
		case 0x09:	// TIMER2H
			// Clear Timer2 Interrupt Flag.
			pMB->sy6522.IFR &= ~IxR_TIMER2;
			UpdateIFR(pMB);

			pMB->sy6522.TIMER2_LATCH.h = nValue;
			pMB->sy6522.TIMER2_COUNTER.w = pMB->sy6522.TIMER2_LATCH.w;
			break;
		case 0x0a:	// SERIAL_SHIFT
			break;
		case 0x0b:	// ACR
			pMB->sy6522.ACR = nValue;
			break;
		case 0x0c:	// PCR -  Used for Speech chip only
			pMB->sy6522.PCR = nValue;
			break;
		case 0x0d:	// IFR
			// - Clear those bits which are set in the lower 7 bits.
			// - Can't clear bit 7 directly.
			nValue |= 0x80;	// Set high bit
			nValue ^= 0x7F;	// Make mask
			pMB->sy6522.IFR &= nValue;
			UpdateIFR(pMB);
			break;
		case 0x0e:	// IER
			if(!(nValue & 0x80))
			{
				// Clear those bits which are set in the lower 7 bits.
				nValue ^= 0x7F;
				pMB->sy6522.IER &= nValue;
				UpdateIFR(pMB);
				
				// Check if timer has been disabled.
				if(pMB->sy6522.IER & IxR_TIMER1)
					break;

				if(pMB->nTimerStatus == 0)
					break;
				
				// Stop timer
				StopTimer(pMB);
			}
			else
			{
				// Set those bits which are set in the lower 7 bits.
				nValue &= 0x7F;
				pMB->sy6522.IER |= nValue;
				UpdateIFR(pMB);
				StartTimer(pMB);
			}
			break;
		case 0x0f:	// ORA_NO_HS
			break;
	}

#if MB_TRACING
        if (mb_trace_fp) {
            _mb_traceSY6522_AY8910(nDevice);
        }
#endif
}

//-----------------------------------------------------------------------------

static uint8_t SY6522_Read(uint8_t nDevice, uint8_t nReg)
{
#if MB_TRACING
        if (mb_trace_fp) {
            fprintf(mb_trace_fp, "\tSY6522_Read(%02X, %02X)...\n", nDevice, nReg);
        }
#endif
//	g_bMB_RegAccessedFlag = true;
	g_bMB_Active = true;

	SY6522_AY8910* pMB = &g_MB[nDevice];
	uint8_t nValue = 0x00;

	switch (nReg)
	{
		case 0x00:	// ORB
			nValue = pMB->sy6522.ORB;
			break;
		case 0x01:	// ORA
			nValue = pMB->sy6522.ORA;
			break;
		case 0x02:	// DDRB
			nValue = pMB->sy6522.DDRB;
			break;
		case 0x03:	// DDRA
			nValue = pMB->sy6522.DDRA;
			break;
		case 0x04:	// TIMER1L_COUNTER
			nValue = pMB->sy6522.TIMER1_COUNTER.l;
			pMB->sy6522.IFR &= ~IxR_TIMER1;		// Also clears Timer1 Interrupt Flag
			UpdateIFR(pMB);
			break;
		case 0x05:	// TIMER1H_COUNTER
			nValue = pMB->sy6522.TIMER1_COUNTER.h;
			break;
		case 0x06:	// TIMER1L_LATCH
			nValue = pMB->sy6522.TIMER1_LATCH.l;
			break;
		case 0x07:	// TIMER1H_LATCH
			nValue = pMB->sy6522.TIMER1_LATCH.h;
			break;
		case 0x08:	// TIMER2L
			nValue = pMB->sy6522.TIMER2_COUNTER.l;
			pMB->sy6522.IFR &= ~IxR_TIMER2;		// Also clears Timer2 Interrupt Flag
			UpdateIFR(pMB);
			break;
		case 0x09:	// TIMER2H
			nValue = pMB->sy6522.TIMER2_COUNTER.h;
			break;
		case 0x0a:	// SERIAL_SHIFT
			break;
		case 0x0b:	// ACR
			nValue = pMB->sy6522.ACR;
			break;
		case 0x0c:	// PCR
			nValue = pMB->sy6522.PCR;
			break;
		case 0x0d:	// IFR
			nValue = pMB->sy6522.IFR;
			break;
		case 0x0e:	// IER
			nValue = 0x80;	// Datasheet says this is 0x80|IER
			break;
		case 0x0f:	// ORA_NO_HS
			nValue = pMB->sy6522.ORA;
			break;
	}

#if MB_TRACING
        if (mb_trace_fp) {
            _mb_traceSY6522_AY8910(nDevice);
            fprintf(mb_trace_fp, "\tret:%02X\n", nValue);
        }
#endif

	return nValue;
}

//---------------------------------------------------------------------------

#if 0 // ENABLE_SSI263
static void SSI263_Play(unsigned int nPhoneme);
#endif

#if 0
typedef struct
{
	BYTE DurationPhoneme;
	BYTE Inflection;		// I10..I3
	BYTE RateInflection;
	BYTE CtrlArtAmp;
	BYTE FilterFreq;
	//
	BYTE CurrentMode;
} SSI263A;
#endif

//static SSI263A nSpeechChip;

// Duration/Phonome
const uint8_t DURATION_MODE_MASK = 0xC0;
const uint8_t PHONEME_MASK = 0x3F;

const uint8_t MODE_PHONEME_TRANSITIONED_INFLECTION = 0xC0;	// IRQ active
const uint8_t MODE_PHONEME_IMMEDIATE_INFLECTION = 0x80;	// IRQ active
const uint8_t MODE_FRAME_IMMEDIATE_INFLECTION = 0x40;		// IRQ active
const uint8_t MODE_IRQ_DISABLED = 0x00;

// Rate/Inflection
const uint8_t RATE_MASK = 0xF0;
const uint8_t INFLECTION_MASK_H = 0x08;	// I11
const uint8_t INFLECTION_MASK_L = 0x07;	// I2..I0

// Ctrl/Art/Amp
const uint8_t CONTROL_MASK = 0x80;
const uint8_t ARTICULATION_MASK = 0x70;
const uint8_t AMPLITUDE_MASK = 0x0F;

#if 0 // ENABLE_SSI263
static uint8_t SSI263_Read(uint8_t nDevice, uint8_t nReg)
{
	SY6522_AY8910* pMB = &g_MB[nDevice];

	// Regardless of register, just return inverted A/!R in bit7
	// . A/!R is low for IRQ

	return pMB->SpeechChip.CurrentMode << 7;
}

static void SSI263_Write(uint8_t nDevice, uint8_t nReg, uint8_t nValue)
{
	SY6522_AY8910* pMB = &g_MB[nDevice];

	switch(nReg)
	{
	case SSI_DURPHON:
#if LOG_SSI263
		LOG("DUR   = 0x%02X, PHON = 0x%02X\n\n", nValue>>6, nValue&PHONEME_MASK);
#endif

		// Datasheet is not clear, but a write to DURPHON must clear the IRQ
		if(g_bPhasorEnable)
		{
#if 1 // APPLE2IX
                    cpu65_uninterrupt(IS_SPEECH);
#else
		    CpuIrqDeassert(IS_SPEECH);
#endif
		}
		else
		{
			pMB->sy6522.IFR &= ~IxR_PERIPHERAL;
			UpdateIFR(pMB);
		}
		pMB->SpeechChip.CurrentMode &= ~1;	// Clear SSI263's D7 pin

		pMB->SpeechChip.DurationPhoneme = nValue;

		g_nSSI263Device = nDevice;

		// Phoneme output not dependent on CONTROL bit
		if(g_bPhasorEnable)
		{
			if(nValue || (g_nCurrentActivePhoneme<0))
				SSI263_Play(nValue & PHONEME_MASK);
		}
		else
		{
			SSI263_Play(nValue & PHONEME_MASK);
		}
		break;
	case SSI_INFLECT:
#if LOG_SSI263
		LOG("INF   = 0x%02X\n", nValue);
#endif
		pMB->SpeechChip.Inflection = nValue;
		break;
	case SSI_RATEINF:
#if LOG_SSI263
		LOG("RATE  = 0x%02X, INF = 0x%02X\n", nValue>>4, nValue&0x0F);
#endif
		pMB->SpeechChip.RateInflection = nValue;
		break;
	case SSI_CTTRAMP:
#if LOG_SSI263
		LOG("CTRL  = %d, ART = 0x%02X, AMP=0x%02X\n", nValue>>7, (nValue&ARTICULATION_MASK)>>4, nValue&AMPLITUDE_MASK);
#endif
		if((pMB->SpeechChip.CtrlArtAmp & CONTROL_MASK) && !(nValue & CONTROL_MASK))	// H->L
			pMB->SpeechChip.CurrentMode = pMB->SpeechChip.DurationPhoneme & DURATION_MODE_MASK;
		pMB->SpeechChip.CtrlArtAmp = nValue;
		break;
	case SSI_FILFREQ:
#if LOG_SSI263
		LOG("FFREQ = 0x%02X\n", nValue);
#endif
		pMB->SpeechChip.FilterFreq = nValue;
		break;
	default:
		break;
	}
}
#endif

//-------------------------------------

static uint8_t Votrax2SSI263[64] = 
{
	0x02,	// 00: EH3 jackEt -> E1 bEnt
	0x0A,	// 01: EH2 Enlist -> EH nEst
	0x0B,	// 02: EH1 hEAvy -> EH1 bElt
	0x00,	// 03: PA0 no sound -> PA
	0x28,	// 04: DT buTTer -> T Tart
	0x08,	// 05: A2 mAde -> A mAde
	0x08,	// 06: A1 mAde -> A mAde
	0x2F,	// 07: ZH aZure -> Z Zero
	0x0E,	// 08: AH2 hOnest -> AH gOt
	0x07,	// 09: I3 inhibIt -> I sIx
	0x07,	// 0A: I2 Inhibit -> I sIx
	0x07,	// 0B: I1 inhIbit -> I sIx
	0x37,	// 0C: M Mat -> More
	0x38,	// 0D: N suN -> N NiNe
	0x24,	// 0E: B Bag -> B Bag
	0x33,	// 0F: V Van -> V Very
	//
	0x32,	// 10: CH* CHip -> SCH SHip (!)
	0x32,	// 11: SH SHop ->  SCH SHip
	0x2F,	// 12: Z Zoo -> Z Zero
	0x10,	// 13: AW1 lAWful -> AW Office
	0x39,	// 14: NG thiNG -> NG raNG
	0x0F,	// 15: AH1 fAther -> AH1 fAther
	0x13,	// 16: OO1 lOOking -> OO lOOk
	0x13,	// 17: OO bOOK -> OO lOOk
	0x20,	// 18: L Land -> L Lift
	0x29,	// 19: K triCK -> Kit
	0x25,	// 1A: J* juDGe -> D paiD (!)
	0x2C,	// 1B: H Hello -> HF Heart
	0x26,	// 1C: G Get -> KV taG
	0x34,	// 1D: F Fast -> F Four
	0x25,	// 1E: D paiD -> D paiD
	0x30,	// 1F: S paSS -> S Same
	//
	0x08,	// 20: A dAY -> A mAde
	0x09,	// 21: AY dAY -> AI cAre
	0x03,	// 22: Y1 Yard -> YI Year
	0x1B,	// 23: UH3 missIOn -> UH3 nUt
	0x0E,	// 24: AH mOp -> AH gOt
	0x27,	// 25: P Past -> P Pen
	0x11,	// 26: O cOld -> O stOre
	0x07,	// 27: I pIn -> I sIx
	0x16,	// 28: U mOve -> U tUne
	0x05,	// 29: Y anY -> AY plEAse
	0x28,	// 2A: T Tap -> T Tart
	0x1D,	// 2B: R Red -> R Roof
	0x01,	// 2C: E mEEt -> E mEEt
	0x23,	// 2D: W Win -> W Water
	0x0C,	// 2E: AE dAd -> AE dAd
	0x0D,	// 2F: AE1 After -> AE1 After
	//
	0x10,	// 30: AW2 sAlty -> AW Office
	0x1A,	// 31: UH2 About -> UH2 whAt
	0x19,	// 32: UH1 Uncle -> UH1 lOve
	0x18,	// 33: UH cUp -> UH wOnder
	0x11,	// 34: O2 fOr -> O stOre
	0x11,	// 35: O1 abOArd -> O stOre
	0x14,	// 36: IU yOU -> IU yOU
	0x14,	// 37: U1 yOU -> IU yOU
	0x35,	// 38: THV THe -> THV THere
	0x36,	// 39: TH THin -> TH wiTH
	0x1C,	// 3A: ER bIrd -> ER bIrd
	0x0A,	// 3B: EH gEt -> EH nEst
	0x01,	// 3C: E1 bE -> E mEEt
	0x10,	// 3D: AW cAll -> AW Office
	0x00,	// 3E: PA1 no sound -> PA
	0x00,	// 3F: STOP no sound -> PA
};

#if 0 // ENABLE_SSI263
static void Votrax_Write(uint8_t nDevice, uint8_t nValue)
{
	g_bVotraxPhoneme = true;

	// !A/R: Acknowledge receipt of phoneme data (signal goes from high to low)
	SY6522_AY8910* pMB = &g_MB[nDevice];
	pMB->sy6522.IFR &= ~IxR_VOTRAX;
	UpdateIFR(pMB);

	g_nSSI263Device = nDevice;

	SSI263_Play(Votrax2SSI263[nValue & PHONEME_MASK]);
}
#endif

//===========================================================================

static void MB_Update()
{
#if 1 // APPLE2IX
    if (!audio_isAvailable) {
        return;
    }

    if (!MockingboardVoice)
    {
        return;
    }

    if (!MockingboardVoice->bActive)
    {
        return;
    }
#   if MB_TRACING
    if (mb_trace_fp) {
        fprintf(mb_trace_fp, "%s", "\tMB_Update()\n");
    }
#   endif
#else
	char szDbg[200];

	if (!MockingboardVoice.bActive)
		return;
#endif

	if (g_bFullSpeed)
	{
#if !MB_TRACING
		// Keep AY reg writes relative to the current 'frame'
		// - Required for Ultima3:
		//   . Tune ends
		//   . g_bFullSpeed:=true (disk-spinning) for ~50 frames
		//   . U3 sets AY_ENABLE:=0xFF (as a side-effect, this sets g_bFullSpeed:=false)
		//   o Without this, the write to AY_ENABLE gets ignored (since AY8910's /g_uLastCumulativeCycles/ was last set 50 frame ago)
		AY8910UpdateSetCycles();

		// TODO:
		// If any AY regs have changed then push them out to the AY chip

		return;
#endif
	}

	//

	if (!g_bMB_RegAccessedFlag)
	{
		if(!g_nMB_InActiveCycleCount)
		{
			g_nMB_InActiveCycleCount = cycles_count_total;
		}
#if 1 // APPLE2IX
		else if(cycles_count_total - g_nMB_InActiveCycleCount > cycles_persec_target/10)
#else
		else if(g_nCumulativeCycles - g_nMB_InActiveCycleCount > (unsigned __int64)g_fCurrentCLK6502/10)
#endif
		{
			// After 0.1 sec of Apple time, assume MB is not active
			g_bMB_Active = false;
		}
	}
	else
	{
		g_nMB_InActiveCycleCount = 0;
		g_bMB_RegAccessedFlag = false;
		g_bMB_Active = true;
	}

	//

#if 0 // !APPLE2IX
	static DWORD dwByteOffset = (DWORD)-1;
#endif
	static int nNumSamplesError = 0;

	const double n6522TimerPeriod = MB_GetFramePeriod();

	const double nIrqFreq = cycles_persec_target / n6522TimerPeriod + 0.5;			// Round-up
	const int nNumSamplesPerPeriod = (int) ((double)SAMPLE_RATE / nIrqFreq);	// Eg. For 60Hz this is 735
	int nNumSamples = nNumSamplesPerPeriod + nNumSamplesError;					// Apply correction
	if(nNumSamples <= 0)
		nNumSamples = 0;
	if(nNumSamples > 2*nNumSamplesPerPeriod)
		nNumSamples = 2*nNumSamplesPerPeriod;

	if(nNumSamples)
		for(int nChip=0; nChip<NUM_AY8910; nChip++)
			AY8910Update(nChip, &ppAYVoiceBuffer[nChip*NUM_VOICES_PER_AY8910], nNumSamples);

	//

#if 1 // APPLE2IX
	unsigned long dwDSLockedBufferSize0 = 0;
	int16_t *pDSLockedBuffer0 = NULL;
	unsigned long dwCurrentPlayCursor;
	int hr = MockingboardVoice->GetCurrentPosition(MockingboardVoice, &dwCurrentPlayCursor);
#else
	DWORD dwDSLockedBufferSize0, dwDSLockedBufferSize1;
	SHORT *pDSLockedBuffer0, *pDSLockedBuffer1;

	DWORD dwCurrentPlayCursor, dwCurrentWriteCursor;
	HRESULT hr = MockingboardVoice.lpDSBvoice->GetCurrentPosition(&dwCurrentPlayCursor, &dwCurrentWriteCursor);
#endif
	if(FAILED(hr))
		return;

#if 0 // !APPLE2IX
	if(dwByteOffset == (DWORD)-1)
	{
		// First time in this func

		dwByteOffset = dwCurrentWriteCursor;
	}
	else
	{
		// Check that our offset isn't between Play & Write positions

		if(dwCurrentWriteCursor > dwCurrentPlayCursor)
		{
			// |-----PxxxxxW-----|
			if((dwByteOffset > dwCurrentPlayCursor) && (dwByteOffset < dwCurrentWriteCursor))
			{
				double fTicksSecs = (double)GetTickCount() / 1000.0;
				sprintf(szDbg, "%010.3f: [MBUpdt]    PC=%08X, WC=%08X, Diff=%08X, Off=%08X, NS=%08X xxx\n", fTicksSecs, dwCurrentPlayCursor, dwCurrentWriteCursor, dwCurrentWriteCursor-dwCurrentPlayCursor, dwByteOffset, nNumSamples);
				OutputDebugString(szDbg);
				if (g_fh) fprintf(g_fh, "%s", szDbg);

				dwByteOffset = dwCurrentWriteCursor;
			}
		}
		else
		{
			// |xxW----------Pxxx|
			if((dwByteOffset > dwCurrentPlayCursor) || (dwByteOffset < dwCurrentWriteCursor))
			{
				double fTicksSecs = (double)GetTickCount() / 1000.0;
				sprintf(szDbg, "%010.3f: [MBUpdt]    PC=%08X, WC=%08X, Diff=%08X, Off=%08X, NS=%08X XXX\n", fTicksSecs, dwCurrentPlayCursor, dwCurrentWriteCursor, dwCurrentWriteCursor-dwCurrentPlayCursor, dwByteOffset, nNumSamples);
				OutputDebugString(szDbg);
				if (g_fh) fprintf(g_fh, "%s", szDbg);

				dwByteOffset = dwCurrentWriteCursor;
			}
		}
	}

	int nBytesRemaining = dwByteOffset - dwCurrentPlayCursor;
#else
	int nBytesRemaining = (int)dwCurrentPlayCursor;
        //LOG("Mockingboard : sound buffer position : %d", nBytesRemaining);
#endif
#if MB_TRACING
        // set nBytesRemaining at a sweet spot for determinism
        nBytesRemaining = g_dwDSBufferSize/4 + 16;
#endif
	if(nBytesRemaining < 0)
		nBytesRemaining += g_dwDSBufferSize;

	// Calc correction factor so that play-buffer doesn't under/overflow
#if 1 // APPLE2IX
        assert(nBytesRemaining >= 0);
	const int nErrorInc = SOUNDCORE_ERROR_INC;
#else
	const int nErrorInc = SoundCore_GetErrorInc();
#endif
	if(nBytesRemaining < g_dwDSBufferSize / 4)
		nNumSamplesError += nErrorInc;				// < 0.25 of buffer remaining
	else if(nBytesRemaining > g_dwDSBufferSize / 2)
		nNumSamplesError -= nErrorInc;				// > 0.50 of buffer remaining
	else
		nNumSamplesError = 0;						// Acceptable amount of data in buffer

#if MB_TRACING
        // assert determinism prevails ...
        assert(nNumSamplesError == 0);
#endif

	if(nNumSamples == 0)
		return;

	//
#if MB_TRACING
        if (mb_trace_fp) {
            fprintf(mb_trace_fp, "\tsubmitting %d samples...\n", nNumSamples);
        }
#endif

	const double fAttenuation = g_bPhasorEnable ? 2.0/3.0 : 1.0;

	for(int i=0; i<nNumSamples; i++)
	{
#if MB_TRACING
#   define TRACING_BUF_SIZ 1024
                size_t tracingBufLSize = TRACING_BUF_SIZ;
                size_t tracingBufRSize = TRACING_BUF_SIZ;
                char tracingBufL[TRACING_BUF_SIZ];
                char *tracingBufLPtr = tracingBufL;
                char tracingBufR[TRACING_BUF_SIZ];
                char *tracingBufRPtr = tracingBufR;
#endif
		// Mockingboard stereo (all voices on an AY8910 wire-or'ed together)
		// L = Address.b7=0, R = Address.b7=1
		int nDataL = 0, nDataR = 0;

		for(unsigned int j=0; j<NUM_VOICES_PER_AY8910; j++)
		{
                        int datL, datR;

			// Slot4
			datL = (int) ((double)ppAYVoiceBuffer[0*NUM_VOICES_PER_AY8910+j][i] * fAttenuation);
			datR = (int) ((double)ppAYVoiceBuffer[1*NUM_VOICES_PER_AY8910+j][i] * fAttenuation);
#if MB_TRACING
                        if (mb_trace_samples_fp) {
                            _mb_traceWriteSample(&tracingBufLPtr, &tracingBufLSize, "+4", datL);
                            _mb_traceWriteSample(&tracingBufRPtr, &tracingBufRSize, "+4", datR);
                        }
#endif
                        nDataL += datL;
                        nDataR += datR;

			// Slot5
			datL = (int) ((double)ppAYVoiceBuffer[2*NUM_VOICES_PER_AY8910+j][i] * fAttenuation);
			datR = (int) ((double)ppAYVoiceBuffer[3*NUM_VOICES_PER_AY8910+j][i] * fAttenuation);
#if MB_TRACING
                        if (mb_trace_samples_fp) {
                            _mb_traceWriteSample(&tracingBufLPtr, &tracingBufLSize, "+5", datL);
                            _mb_traceWriteSample(&tracingBufRPtr, &tracingBufRSize, "+5", datR);
                        }
#endif
                        nDataL += datL;
                        nDataR += datR;
		}

#if MB_TRACING
                if (mb_trace_samples_fp) {
                    _mb_traceWriteSample(&tracingBufLPtr, &tracingBufLSize, "==", nDataL);
                    _mb_traceWriteSample(&tracingBufRPtr, &tracingBufRSize, "==", nDataR);
                }
#endif

		// Cap the superpositioned output
		if(nDataL < nWaveDataMin)
			nDataL = nWaveDataMin;
		else if(nDataL > nWaveDataMax)
			nDataL = nWaveDataMax;

		if(nDataR < nWaveDataMin)
			nDataR = nWaveDataMin;
		else if(nDataR > nWaveDataMax)
			nDataR = nWaveDataMax;

#if MB_TRACING
                if (mb_trace_samples_fp) {
                    _mb_traceWriteSample(&tracingBufLPtr, &tracingBufLSize, "=>", nDataL);
                    _mb_traceWriteSample(&tracingBufRPtr, &tracingBufRSize, "=>", nDataR);
                    fprintf(mb_trace_samples_fp, "L:%s\nR:%s\n", tracingBufL, tracingBufR);
                }
#endif

		g_nMixBuffer[i*g_nMB_NumChannels+0] = (short)nDataL * samplesScale;	// L
		g_nMixBuffer[i*g_nMB_NumChannels+1] = (short)nDataR * samplesScale;	// R
	}

	//

#if 0 // !APPLE2IX
	if(!DSGetLock(MockingboardVoice.lpDSBvoice,
						dwByteOffset, (DWORD)nNumSamples*sizeof(short)*g_nMB_NumChannels,
						&pDSLockedBuffer0, &dwDSLockedBufferSize0,
						&pDSLockedBuffer1, &dwDSLockedBufferSize1))
		return;

	memcpy(pDSLockedBuffer0, &g_nMixBuffer[0], dwDSLockedBufferSize0);
	if(pDSLockedBuffer1)
		memcpy(pDSLockedBuffer1, &g_nMixBuffer[dwDSLockedBufferSize0/sizeof(short)], dwDSLockedBufferSize1);

	// Commit sound buffer
	hr = MockingboardVoice.lpDSBvoice->Unlock((void*)pDSLockedBuffer0, dwDSLockedBufferSize0,
											  (void*)pDSLockedBuffer1, dwDSLockedBufferSize1);

	dwByteOffset = (dwByteOffset + (DWORD)nNumSamples*sizeof(short)*g_nMB_NumChannels) % g_dwDSBufferSize;
#else
        const unsigned long originalRequestedBufSize = (unsigned long)nNumSamples*sizeof(short)*g_nMB_NumChannels;
        unsigned long requestedBufSize = originalRequestedBufSize;
        unsigned long bufIdx = 0;
        unsigned long counter = 0;

        if (!nNumSamples) {
            return;
        }

#   if !MB_TRACING
        // make at least 2 attempts to submit data (could be at a ringBuffer boundary)
        do {
            if (MockingboardVoice->Lock(MockingboardVoice, requestedBufSize, &pDSLockedBuffer0, &dwDSLockedBufferSize0)) {
                return;
            }

            {
                unsigned long modTwo = (dwDSLockedBufferSize0 % 2);
                assert(modTwo == 0);
            }
            memcpy(pDSLockedBuffer0, &g_nMixBuffer[bufIdx/sizeof(short)], dwDSLockedBufferSize0);
            MockingboardVoice->Unlock(MockingboardVoice, dwDSLockedBufferSize0);
            bufIdx += dwDSLockedBufferSize0;
            requestedBufSize -= dwDSLockedBufferSize0;
            assert(requestedBufSize <= originalRequestedBufSize);
            ++counter;
        } while (bufIdx < originalRequestedBufSize && counter < 2);
        assert(bufIdx == originalRequestedBufSize);
#   endif
#endif

#ifdef RIFF_MB
	RiffPutSamples(&g_nMixBuffer[0], nNumSamples);
#endif
}

//-----------------------------------------------------------------------------

#if 0 // ENABLE_SSI263
#if 0 // !APPLE2IX
static DWORD WINAPI SSI263Thread(LPVOID lpParameter)
{
	while(1)
	{
		DWORD dwWaitResult = WaitForMultipleObjects( 
								g_nNumEvents,		// number of handles in array
								g_hSSI263Event,		// array of event handles
								FALSE,				// wait until any one is signaled
								INFINITE);

		if((dwWaitResult < WAIT_OBJECT_0) || (dwWaitResult > WAIT_OBJECT_0+g_nNumEvents-1))
			continue;

		dwWaitResult -= WAIT_OBJECT_0;			// Determine event # that signaled

		if(dwWaitResult == (g_nNumEvents-1))	// Termination event
			break;
#else
static void* SSI263Thread(void *lpParameter)
{
        const unsigned long nsecWait = NANOSECONDS_PER_SECOND / audio_backend->systemSettings.sampleRateHz;
        const struct timespec wait = { .tv_sec=0, .tv_nsec=nsecWait };

	while(1)
	{
            int err =0;

            pthread_mutex_lock(&ssi263_mutex);
            err = pthread_cond_timedwait(&ssi263_cond, &ssi263_mutex, &wait);
            if (err && (err != ETIMEDOUT))
            {
                ERRLOG("OOPS pthread_cond_timedwait");
            }
            pthread_mutex_unlock(&ssi263_mutex);

            if (quit_event)
            {
                break;
            }

            // poll to see if any samples finished ...
            bool sample_finished = false;
            for (unsigned int i=0; i<64; i++)
            {
		if (SSI263Voice[i] && SSI263Voice[i]->bActive)
                {
                    unsigned long status = 0;
                    SSI263Voice[i]->GetStatus(SSI263Voice[i], &status);

                    if (status & AUDIO_STATUS_NOTPLAYING)
                    {
                        sample_finished = true;
                        break;
                    }
                }
            }
            if (!sample_finished)
            {
                continue;
            }
#endif
		// Phoneme completed playing

		if (g_bStopPhoneme)
		{
			g_bStopPhoneme = false;
			continue;
		}

#if LOG_SSI263
		//if(g_fh) fprintf(g_fh, "IRQ: Phoneme complete (0x%02X)\n\n", g_nCurrentActivePhoneme);
#endif

		SSI263Voice[g_nCurrentActivePhoneme]->bActive = false;
		g_nCurrentActivePhoneme = -1;

		// Phoneme complete, so generate IRQ if necessary
		SY6522_AY8910* pMB = &g_MB[g_nSSI263Device];

		if(g_bPhasorEnable)
		{
			if((pMB->SpeechChip.CurrentMode != MODE_IRQ_DISABLED))
			{
				pMB->SpeechChip.CurrentMode |= 1;	// Set SSI263's D7 pin

				// Phasor's SSI263.IRQ line appears to be wired directly to IRQ (Bypassing the 6522)
#if 1 // APPLE2IX
                                cpu65_interrupt(IS_SPEECH);
#else
				CpuIrqAssert(IS_SPEECH);
#endif
			}
		}
		else
		{
			if((pMB->SpeechChip.CurrentMode != MODE_IRQ_DISABLED) && (pMB->sy6522.PCR == 0x0C))
			{
				pMB->sy6522.IFR |= IxR_PERIPHERAL;
				UpdateIFR(pMB);
				pMB->SpeechChip.CurrentMode |= 1;	// Set SSI263's D7 pin
			}
		}

		//

		if(g_bVotraxPhoneme && (pMB->sy6522.PCR == 0xB0))
		{
			// !A/R: Time-out of old phoneme (signal goes from low to high)

			pMB->sy6522.IFR |= IxR_VOTRAX;
			UpdateIFR(pMB);

			g_bVotraxPhoneme = false;
		}
	}

	return 0;
}

//-----------------------------------------------------------------------------

static void SSI263_Play(unsigned int nPhoneme)
{
#if 1
	HRESULT hr;

	{
		int nCurrPhoneme = g_nCurrentActivePhoneme;	// local copy in case SSI263Thread sets it to -1
		if (nCurrPhoneme >= 0)
		{
			// A write to DURPHON before previous phoneme has completed
			g_bStopPhoneme = true;
			hr = SSI263Voice[nCurrPhoneme].lpDSBvoice->Stop();

			// Busy-wait until ACK from SSI263Thread
			// . required to avoid data-race
			while (	g_bStopPhoneme &&				// wait for SSI263Thread to ACK the lpDSBVoice->Stop()
					g_nCurrentActivePhoneme >= 0)	// wait for SSI263Thread to get end of sample event
				;

			g_bStopPhoneme = false;
		}
	}

	g_nCurrentActivePhoneme = nPhoneme;

	hr = SSI263Voice[g_nCurrentActivePhoneme].lpDSBvoice->SetCurrentPosition(0);
	if(FAILED(hr))
		return;

	hr = SSI263Voice[g_nCurrentActivePhoneme].lpDSBvoice->Play(0,0,0);	// Not looping
	if(FAILED(hr))
		return;

	SSI263Voice[g_nCurrentActivePhoneme].bActive = true;
#else
	HRESULT hr;
	bool bPause;

	if(nPhoneme == 1)
		nPhoneme = 2;	// Missing this sample, so map to phoneme-2

	if(nPhoneme == 0)
	{
		bPause = true;
	}
	else
	{
//		nPhoneme--;
		nPhoneme-=2;	// Missing phoneme-1
		bPause = false;
	}

	DWORD dwDSLockedBufferSize = 0;    // Size of the locked DirectSound buffer
	SHORT* pDSLockedBuffer;

	hr = SSI263Voice.lpDSBvoice->Stop();

	if(!DSGetLock(SSI263Voice.lpDSBvoice, 0, 0, &pDSLockedBuffer, &dwDSLockedBufferSize, NULL, 0))
		return;

	unsigned int nPhonemeShortLength = g_nPhonemeInfo[nPhoneme].nLength;
	unsigned int nPhonemeByteLength = g_nPhonemeInfo[nPhoneme].nLength * sizeof(SHORT);

	if(bPause)
	{
		// 'pause' length is length of 1st phoneme (arbitrary choice, since don't know real length)
		memset(pDSLockedBuffer, 0, g_dwMaxPhonemeLen);
	}
	else
	{
		memcpy(pDSLockedBuffer, &g_nPhonemeData[g_nPhonemeInfo[nPhoneme].nOffset], nPhonemeByteLength);
		memset(&pDSLockedBuffer[nPhonemeShortLength], 0, g_dwMaxPhonemeLen-nPhonemeByteLength);
	}

#if 0
	DSBPOSITIONNOTIFY PositionNotify;

	PositionNotify.dwOffset = nPhonemeByteLength - 1;		// End of phoneme
	PositionNotify.hEventNotify = g_hSSI263Event[0];

	hr = SSI263Voice.lpDSNotify->SetNotificationPositions(1, &PositionNotify);
	if(FAILED(hr))
	{
		DirectSound_ErrorText(hr);
		return;
	}
#endif

	hr = SSI263Voice.lpDSBvoice->Unlock((void*)pDSLockedBuffer, dwDSLockedBufferSize, NULL, 0);
	if(FAILED(hr))
		return;

	hr = SSI263Voice.lpDSBvoice->Play(0,0,0);	// Not looping
	if(FAILED(hr))
		return;

	SSI263Voice.bActive = true;
#endif
}
#endif // ENABLE_SSI263

//-----------------------------------------------------------------------------

static bool MB_DSInit()
{
#if 1 // APPLE2IX
	LOG("MB_DSInit : %d\n", g_bMBAvailable);
#else
	LogFileOutput("MB_DSInit\n", g_bMBAvailable);
#endif
#ifdef NO_DIRECT_X

	return false;

#else // NO_DIRECT_X

	//
	// Create single Mockingboard voice
	//

	unsigned long dwDSLockedBufferSize = 0;    // Size of the locked DirectSound buffer
	int16_t* pDSLockedBuffer;

	if(!audio_isAvailable)
		return false;

	int hr = audio_createSoundBuffer(&MockingboardVoice);
	LOG("MB_DSInit: DSGetSoundBuffer(), hr=0x%08X\n", (unsigned int)hr);
	if(FAILED(hr))
	{
		LOG("MB: DSGetSoundBuffer failed (%08X)\n",(unsigned int)hr);
		return false;
	}

#if 1 // APPLE2IX
        SAMPLE_RATE = audio_backend->systemSettings.sampleRateHz;
#if MB_TRACING
        // force determinism
        SAMPLE_RATE = 44100;
#endif
        g_dwDSBufferSize = audio_backend->systemSettings.stereoBufferSizeSamples * audio_backend->systemSettings.bytesPerSample * g_nMB_NumChannels;
        g_nMixBuffer = MALLOC(g_dwDSBufferSize / audio_backend->systemSettings.bytesPerSample);

#else
	bool bRes = DSZeroVoiceBuffer(&MockingboardVoice, "MB", g_dwDSBufferSize);
	LogFileOutput("MB_DSInit: DSZeroVoiceBuffer(), res=%d\n", bRes ? 1 : 0);
	if (!bRes)
		return false;
#endif

	MockingboardVoice->bActive = true;

	// Volume might've been setup from value in Registry
	if(!MockingboardVoice->nVolume)
		MockingboardVoice->nVolume = DSBVOLUME_MAX;

#if 0 // !APPLE2IX
	hr = MockingboardVoice.lpDSBvoice->SetVolume(MockingboardVoice.nVolume);
	LogFileOutput("MB_DSInit: SetVolume(), hr=0x%08X\n", hr);
#endif

	//---------------------------------

	//
	// Create SSI263 voice
	//

#if 0
	g_dwMaxPhonemeLen = 0;
	for(int i=0; i<sizeof(g_nPhonemeInfo) / sizeof(PHONEME_INFO); i++)
		if(g_dwMaxPhonemeLen < g_nPhonemeInfo[i].nLength)
			g_dwMaxPhonemeLen = g_nPhonemeInfo[i].nLength;
	g_dwMaxPhonemeLen *= sizeof(SHORT);
#endif

#if 1 // APPLE2IX
        int err = 0;
        if ((err = pthread_mutex_init(&ssi263_mutex, NULL)))
        {
            ERRLOG("OOPS pthread_mutex_init");
        }

        if ((err = pthread_cond_init(&ssi263_cond, NULL)))
        {
            ERRLOG("OOPS pthread_cond_init");
        }
#else
	g_hSSI263Event[0] = CreateEvent(NULL,	// lpEventAttributes
									FALSE,	// bManualReset (FALSE = auto-reset)
									FALSE,	// bInitialState (FALSE = non-signaled)
									NULL);	// lpName
	LogFileOutput("MB_DSInit: CreateEvent(), g_hSSI263Event[0]=0x%08X\n", (UINT32)g_hSSI263Event[0]);

	g_hSSI263Event[1] = CreateEvent(NULL,	// lpEventAttributes
									FALSE,	// bManualReset (FALSE = auto-reset)
									FALSE,	// bInitialState (FALSE = non-signaled)
									NULL);	// lpName
	LogFileOutput("MB_DSInit: CreateEvent(), g_hSSI263Event[1]=0x%08X\n", (UINT32)g_hSSI263Event[1]);

	if((g_hSSI263Event[0] == NULL) || (g_hSSI263Event[1] == NULL))
	{
		if(g_fh) fprintf(g_fh, "SSI263: CreateEvent failed\n");
		return false;
	}
#endif

#if 0 // ENABLE_SSI263
#warning FIXME TODO : this needs to be properly implemented ...

	for(int i=0; i<64; i++)
	{
		unsigned int nPhoneme = i;
		bool bPause;

		if(nPhoneme == 1)
			nPhoneme = 2;	// Missing this sample, so map to phoneme-2

		if(nPhoneme == 0)
		{
			bPause = true;
		}
		else
		{
//			nPhoneme--;
			nPhoneme-=2;	// Missing phoneme-1
			bPause = false;
		}

		unsigned int nPhonemeByteLength = g_nPhonemeInfo[nPhoneme].nLength * audio_backend->systemSettings.bytesPerSample;
#if 0 // !APPLE2IX
		// NB. DSBCAPS_LOCSOFTWARE required for Phoneme+2==0x28 - sample too short (see KB327698)
		hr = DSGetSoundBuffer(&SSI263Voice[i], DSBCAPS_CTRLVOLUME+DSBCAPS_CTRLPOSITIONNOTIFY+DSBCAPS_LOCSOFTWARE, nPhonemeByteLength, 22050, 1);
		LogFileOutput("MB_DSInit: (%02d) DSGetSoundBuffer(), hr=0x%08X\n", i, hr);
#else
                if (nPhonemeByteLength > audio_backend->systemSettings.monoBufferSizeSamples) {
                    RELEASE_ERRLOG("!!!!!!!!!!!!!!!!!!!!! phoneme length > buffer size !!!!!!!!!!!!!!!!!!!!!");
#warning ^^^^^^^^^^ require vigilence here around this change ... we used to be able to specify the exact buffer size ...
                }
                nPhonemeByteLength = dwDSLockedBufferSize;

		// NB. DSBCAPS_LOCSOFTWARE required for
		hr = audio_createSoundBuffer(&SSI263Voice[i], 1);
		LOG("MB_DSInit: (%02d) DSGetSoundBuffer(), hr=0x%08X\n", i, (unsigned int)hr);
#endif
		if(FAILED(hr))
		{
			LOG("SSI263: DSGetSoundBuffer failed (%08X)\n",(unsigned int)hr);
			return false;
		}

		hr = SSI263Voice[i]->Lock(SSI263Voice[i], 0, &pDSLockedBuffer, &dwDSLockedBufferSize);
		//LogFileOutput("MB_DSInit: (%02d) DSGetLock(), res=%d\n", i, bRes ? 1 : 0);	// WARNING: Lock acquired && doing heavy-weight logging
		if(FAILED(hr))
		{
			LOG("SSI263: DSGetLock failed (%08X)\n",(unsigned int)hr);
			return false;
		}

		if(bPause)
		{
			// 'pause' length is length of 1st phoneme (arbitrary choice, since don't know real length)
			memset(pDSLockedBuffer, 0x00, nPhonemeByteLength);
		}
		else
		{
			memcpy(pDSLockedBuffer, &g_nPhonemeData[g_nPhonemeInfo[nPhoneme].nOffset], nPhonemeByteLength);
		}

#if 1 // APPLE2IX
#error FIXME TODO : need a way to notify sound finished and remove the bullshit polling
                // Assume no way to get notification of sound finished, instead we will poll from mockingboard thread ...
#else
 		hr = SSI263Voice[i].lpDSBvoice->QueryInterface(IID_IDirectSoundNotify, (LPVOID *)&SSI263Voice[i].lpDSNotify);
		//LogFileOutput("MB_DSInit: (%02d) QueryInterface(), hr=0x%08X\n", i, hr);	// WARNING: Lock acquired && doing heavy-weight logging
		if(FAILED(hr))
		{
			if(g_fh) fprintf(g_fh, "SSI263: QueryInterface failed (%08X)\n",hr);
			return false;
		}

		DSBPOSITIONNOTIFY PositionNotify;

//		PositionNotify.dwOffset = nPhonemeByteLength - 1;	// End of buffer
		PositionNotify.dwOffset = DSBPN_OFFSETSTOP;			// End of buffer
		PositionNotify.hEventNotify = g_hSSI263Event[0];

		hr = SSI263Voice[i].lpDSNotify->SetNotificationPositions(1, &PositionNotify);
		//LogFileOutput("MB_DSInit: (%02d) SetNotificationPositions(), hr=0x%08X\n", i, hr);	// WARNING: Lock acquired && doing heavy-weight logging
		if(FAILED(hr))
		{
			if(g_fh) fprintf(g_fh, "SSI263: SetNotifyPos failed (%08X)\n",hr);
			return false;
		}
#endif

#if 1 // APPLE2IX
		hr = SSI263Voice[i]->UnlockStaticBuffer(SSI263Voice[i], dwDSLockedBufferSize);
		LOG("MB_DSInit: (%02d) Unlock(),hr=0x%08X\n", i, (unsigned int)hr);
#else
		hr = SSI263Voice[i].lpDSBvoice->Unlock((void*)pDSLockedBuffer, dwDSLockedBufferSize, NULL, 0);
		LogFileOutput("MB_DSInit: (%02d) Unlock(),hr=0x%08X\n", i, hr);
#endif
		if(FAILED(hr))
		{
			LOG("SSI263: DSUnlock failed (%08X)\n",(unsigned int)hr);
			return false;
		}

		SSI263Voice[i]->bActive = false;
		SSI263Voice[i]->nVolume = MockingboardVoice->nVolume;		// Use same volume as MB
#if 0 // !APPLE2IX
		hr = SSI263Voice[i].lpDSBvoice->SetVolume(SSI263Voice[i].nVolume);
		LogFileOutput("MB_DSInit: (%02d) SetVolume(), hr=0x%08X\n", i, hr);
#endif
	}

	//

	unsigned long dwThreadId;

#if 1 // APPLE2IX
        {
            int err = 0;
            if ((err = pthread_create(&g_hThread, NULL, SSI263Thread, NULL)))
            {
                ERRLOG("SSI263Thread");
            }

            // assuming time critical ...
#   if defined(__APPLE__) || defined(ANDROID)
#   warning possible FIXME possible TODO : set thread priority in Darwin/Mach
#   else
            int policy = sched_getscheduler(getpid());

            int prio = 0;
            if ((prio = sched_get_priority_max(policy)) < 0) {
                ERRLOG("OOPS sched_get_priority_max");
            } else {
                if ((err = pthread_setschedprio(thread, prio)))
                {
                    ERRLOG("OOPS pthread_setschedprio");
                }
            }
#   endif
        }
#else
	g_hThread = CreateThread(NULL,				// lpThreadAttributes
								0,				// dwStackSize
								SSI263Thread,
								NULL,			// lpParameter
								0,				// dwCreationFlags : 0 = Run immediately
								&dwThreadId);	// lpThreadId
	LOG("MB_DSInit: CreateThread(), g_hThread=0x%08X\n", (uint32_t)g_hThread);

	bool bRes2 = SetThreadPriority(g_hThread, THREAD_PRIORITY_TIME_CRITICAL);
	LOG("MB_DSInit: SetThreadPriority(), bRes=%d\n", bRes2 ? 1 : 0);
#endif

#endif // FIXME : ENABLE_SSI263
	return true;

#endif // NO_DIRECT_X
}

static void MB_DSUninit()
{
	if(g_hThread)
	{
#if 1 // APPLE2IX
                quit_event = true;
                pthread_cond_signal(&ssi263_cond);

                int err = 0;
                if ( (err = pthread_join(g_hThread, NULL)) ) {
                    ERRLOG("OOPS pthread_join");
                }
#else
		unsigned long dwExitCode;
		SetEvent(g_hSSI263Event[g_nNumEvents-1]);	// Signal to thread that it should exit

		do
		{
			if(GetExitCodeThread(g_hThread, &dwExitCode))
			{
				if(dwExitCode == STILL_ACTIVE)
					usleep(10);
				else
					break;
			}
		}
		while(1);
#endif

#if 1 // APPLE2IX
		g_hThread = 0;
                pthread_mutex_destroy(&ssi263_mutex);
                pthread_cond_destroy(&ssi263_cond);
#else
		CloseHandle(g_hThread);
		g_hThread = NULL;
#endif
	}

	//

	if(MockingboardVoice && MockingboardVoice->bActive)
	{
#if 0 // !APPLE2IX
		MockingboardVoice.lpDSBvoice->Stop();
#endif
		MockingboardVoice->bActive = false;
	}

	audio_destroySoundBuffer(&MockingboardVoice);

	//

	for(int i=0; i<64; i++)
	{
		if(SSI263Voice[i] && SSI263Voice[i]->bActive)
		{
#if 0 // !APPLE2IX
			SSI263Voice[i].lpDSBvoice->Stop();
#endif
			SSI263Voice[i]->bActive = false;
		}

		audio_destroySoundBuffer(&SSI263Voice[i]);
	}

	//

#if 1 // APPLE2IX
        FREE(g_nMixBuffer);
#else
	if(g_hSSI263Event[0])
	{
		CloseHandle(g_hSSI263Event[0]);
		g_hSSI263Event[0] = NULL;
	}

	if(g_hSSI263Event[1])
	{
		CloseHandle(g_hSSI263Event[1]);
		g_hSSI263Event[1] = NULL;
	}
#endif
}

//=============================================================================

//
// ----- ALL GLOBALLY ACCESSIBLE FUNCTIONS ARE BELOW THIS LINE -----
//

//=============================================================================

void MB_Initialize()
{
#if 1 // APPLE2IX
    assert(pthread_self() == cpu_thread_id);
    memset(SSI263Voice, 0x0, sizeof(AudioBuffer_s *) * 64);
#endif
	LOG("MB_Initialize: g_bDisableDirectSound=%d, g_bDisableDirectSoundMockingboard=%d\n", g_bDisableDirectSound, g_bDisableDirectSoundMockingboard);
	if (g_bDisableDirectSound || g_bDisableDirectSoundMockingboard)
	{
#if 0 // !APPLE2IX
		MockingboardVoice.bMute = true;
#endif
		g_SoundcardType = CT_Empty;
	}
	else
	{
		memset(&g_MB,0,sizeof(g_MB));

#if 1 // APPLE2IX
		g_bMBAvailable = MB_DSInit();
                if (!g_bMBAvailable) {
                    //MockingboardVoice->bMute = true;
                    g_SoundcardType = CT_Empty;
                    return;
                }
#endif

		int i;
		for(i=0; i<NUM_VOICES; i++)
#if 1 // APPLE2IX
			ppAYVoiceBuffer[i] = MALLOC(sizeof(short) * SAMPLE_RATE); // Buffer can hold a max of 1 seconds worth of samples
#else
			ppAYVoiceBuffer[i] = new short [SAMPLE_RATE];	// Buffer can hold a max of 1 seconds worth of samples
#endif

		AY8910_InitAll((int)cycles_persec_target, SAMPLE_RATE);
		LOG("MB_Initialize: AY8910_InitAll()\n");

		for(i=0; i<NUM_AY8910; i++)
			g_MB[i].nAY8910Number = i;

		//

#if 0 // !APPLE2IX
		g_bMBAvailable = MB_DSInit();
#endif
		LOG("MB_Initialize: MB_DSInit(), g_bMBAvailable=%d\n", g_bMBAvailable);

		MB_Reset();
		LOG("MB_Initialize: MB_Reset()\n");
	}
}

#if 1 // APPLE2IX
// HACK functions for "soft" destroying backend audio resource (but keeping current state)
void MB_SoftDestroy(void) {
    assert(pthread_self() == cpu_thread_id);
    MB_DSUninit();
}
void MB_SoftInitialize(void) {
    assert(pthread_self() == cpu_thread_id);
    MB_DSInit();
}
#endif

//-----------------------------------------------------------------------------

// NB. Called when /cycles_persec_target/ changes
void MB_Reinitialize()
{
#if 1 // APPLE2IX
	AY8910_InitClock((int)cycles_persec_target, SAMPLE_RATE);
#else
	AY8910_InitClock((int)g_fCurrentCLK6502);	// todo: account for g_PhasorClockScaleFactor?
#endif
												// NB. Other calls to AY8910_InitClock() use the constant CLK_6502
}

//-----------------------------------------------------------------------------

void MB_Destroy()
{
#if 1 // APPLE2IX
    assert(pthread_self() == cpu_thread_id);
#endif
	MB_DSUninit();

	for(int i=0; i<NUM_VOICES; i++)
        {
#if 1 // APPLE2IX
		FREE(ppAYVoiceBuffer[i]);
#else
		delete [] ppAYVoiceBuffer[i];
#endif
        }
}

#if 1 // APPLE2IX
// HACK NOTE TODO FIXME : hardcoded for now (until we have dynamic emulation for other cards in these slots) ...
//SS_CARDTYPE g_Slot4 = CT_Phasor;
//SS_CARDTYPE g_Slot5 = CT_Empty;
SS_CARDTYPE g_Slot4 = CT_MockingboardC;
SS_CARDTYPE g_Slot5 = CT_MockingboardC;
void MB_SetEnabled(bool enabled) {
    g_bDisableDirectSoundMockingboard = !enabled;
    g_SoundcardType = enabled ? CT_MockingboardC : CT_Empty;
    g_Slot4 = enabled ? CT_MockingboardC : CT_Empty;
    g_Slot5 = enabled ? CT_MockingboardC : CT_Empty;
}

bool MB_ISEnabled(void) {
    return (MockingboardVoice != NULL);
}
#endif

//-----------------------------------------------------------------------------

static void ResetState()
{
	g_n6522TimerPeriod = 0;
	g_nMBTimerDevice = TIMERDEVICE_INVALID;
	g_uLastCumulativeCycles = 0;

	g_nSSI263Device = 0;
	g_nCurrentActivePhoneme = -1;
	g_bStopPhoneme = false;
	g_bVotraxPhoneme = false;

	g_nMB_InActiveCycleCount = 0;
	g_bMB_RegAccessedFlag = false;
	g_bMB_Active = false;

	//g_bMBAvailable = false;

//	g_SoundcardType = CT_Empty;	// Don't uncomment, else _ASSERT will fire in MB_Read() after an F2->MB_Reset()
//	g_bPhasorEnable = false;
	g_nPhasorMode = 0;
	g_PhasorClockScaleFactor = 1;
}

void MB_Reset()
{
	if(!audio_isAvailable)
		return;

	for(int i=0; i<NUM_AY8910; i++)
	{
		ResetSY6522(&g_MB[i]);
		AY8910_reset(i);
	}

	ResetState();
	MB_Reinitialize();	// Reset CLK for AY8910s
}

//-----------------------------------------------------------------------------

#if 1 // APPLE2IX
#define MemReadFloatingBus floating_bus
#define nAddr ea
GLUE_C_READ(MB_Read)
{
    return mb_read(ea);
}

uint8_t mb_read(uint16_t ea)
#else
static BYTE __stdcall MB_Read(WORD PC, WORD nAddr, BYTE bWrite, BYTE nValue, ULONG nCyclesLeft)
#endif
{
#if 1 // APPLE2IX
#   if MB_TRACING
    if (mb_trace_fp) {
        fprintf(mb_trace_fp, "MB_Read|%04X\n", ea);
    }
#   endif
	MB_UpdateCycles();
#else
	MB_UpdateCycles(nCyclesLeft);
#endif

#if 0 // _DEBUG
	if(!IS_APPLE2 && !MemCheckSLOTCXROM())
	{
		_ASSERT(0);	// Card ROM disabled, so IORead_Cxxx() returns the internal ROM
		return mem[nAddr];
	}

	if(g_SoundcardType == CT_Empty)
	{
		_ASSERT(0);	// Card unplugged, so IORead_Cxxx() returns the floating bus
		return MemReadFloatingBus(nCyclesLeft);
	}
#endif

	uint8_t nMB = ((nAddr>>8)&0xf) - SLOT4;
	uint8_t nOffset = nAddr&0xff;

	if(g_bPhasorEnable)
	{
		if(nMB != 0)	// Slot4 only
#if 1 // APPLE2IX
			return MemReadFloatingBus();
#else
			return MemReadFloatingBus(nCyclesLeft);
#endif

		int CS;
		if(g_nPhasorMode & 1)
			CS = ( ( nAddr & 0x80 ) >> 6 ) | ( ( nAddr & 0x10 ) >> 4 );	// 0, 1, 2 or 3
		else															// Mockingboard Mode
			CS = ( ( nAddr & 0x80 ) >> 7 ) + 1;							// 1 or 2

		uint8_t nRes = 0;

		if(CS & 1)
			nRes |= SY6522_Read(nMB*NUM_DEVS_PER_MB + SY6522_DEVICE_A, nAddr&0xf);

		if(CS & 2)
			nRes |= SY6522_Read(nMB*NUM_DEVS_PER_MB + SY6522_DEVICE_B, nAddr&0xf);

		bool bAccessedDevice = (CS & 3) ? true : false;

		if((nOffset >= SSI263_Offset) && (nOffset <= (SSI263_Offset+0x05)))
		{
#if 0 // ENABLE_SSI263
			nRes |= SSI263_Read(nMB, nAddr&0xf);
#else
                        mb_assert(false);
#endif
			bAccessedDevice = true;
		}

#if 1 // APPLE2IX
		return bAccessedDevice ? nRes : MemReadFloatingBus();
#else
		return bAccessedDevice ? nRes : MemReadFloatingBus(nCyclesLeft);
#endif
	}

	if(nOffset <= (SY6522A_Offset+0x0F))
		return SY6522_Read(nMB*NUM_DEVS_PER_MB + SY6522_DEVICE_A, nAddr&0xf);
	else if((nOffset >= SY6522B_Offset) && (nOffset <= (SY6522B_Offset+0x0F)))
		return SY6522_Read(nMB*NUM_DEVS_PER_MB + SY6522_DEVICE_B, nAddr&0xf);
	else if((nOffset >= SSI263_Offset) && (nOffset <= (SSI263_Offset+0x05)))
#if 0 // ENABLE_SSI263
		return SSI263_Read(nMB, nAddr&0xf);
#else
                mb_assert(false);
#endif

#if MB_TRACING
#   if 1 // APPLE2IX
        uint8_t b = MemReadFloatingBus();
#   else
        BYTE b = MemReadFloatingBus(nCyclesLeft);
#   endif
        if (mb_trace_fp) {
            fprintf(mb_trace_fp, "\tfall through ret:%02X\n", b);
        }
        return b;
#else
#   if 1 // APPLE2IX
		return MemReadFloatingBus();
#   else
		return MemReadFloatingBus(nCyclesLeft);
#   endif
#endif
}

//-----------------------------------------------------------------------------

#if 1 // APPLE2IX
#define nValue b
GLUE_C_WRITE(MB_Write)
#else
static BYTE __stdcall MB_Write(WORD PC, WORD nAddr, BYTE bWrite, BYTE nValue, ULONG nCyclesLeft)
#endif
{
#if 1 // APPLE2IX
#   if MB_TRACING
    if (mb_trace_fp) {
        fprintf(mb_trace_fp, "MB_Write|%04X|%02X\n", ea, b);
    }
#   endif
	MB_UpdateCycles();
#else
	MB_UpdateCycles(nCyclesLeft);
#endif

#if 0 // _DEBUG
	if(!IS_APPLE2 && !MemCheckSLOTCXROM())
	{
		_ASSERT(0);	// Card ROM disabled, so IORead_Cxxx() returns the internal ROM
		return 0;
	}

	if(g_SoundcardType == CT_Empty)
	{
		_ASSERT(0);	// Card unplugged, so IORead_Cxxx() returns the floating bus
		return 0;
	}
#endif

	uint8_t nMB = ((nAddr>>8)&0xf) - SLOT4;
	uint8_t nOffset = nAddr&0xff;

	if(g_bPhasorEnable)
	{
		if(nMB != 0)	// Slot4 only
			return/*0*/;

		int CS;

		if(g_nPhasorMode & 1)
			CS = ( ( nAddr & 0x80 ) >> 6 ) | ( ( nAddr & 0x10 ) >> 4 );	// 0, 1, 2 or 3
		else															// Mockingboard Mode
			CS = ( ( nAddr & 0x80 ) >> 7 ) + 1;							// 1 or 2

		if(CS & 1)
			SY6522_Write(nMB*NUM_DEVS_PER_MB + SY6522_DEVICE_A, nAddr&0xf, nValue);

		if(CS & 2)
			SY6522_Write(nMB*NUM_DEVS_PER_MB + SY6522_DEVICE_B, nAddr&0xf, nValue);

		if((nOffset >= SSI263_Offset) && (nOffset <= (SSI263_Offset+0x05)))
#if 0 // ENABLE_SSI263
			SSI263_Write(nMB*2+1, nAddr&0xf, nValue);		// Second 6522 is used for speech chip
#else
                        mb_assert(false);
#endif

		return/*0*/;
	}

	if(nOffset <= (SY6522A_Offset+0x0F))
		SY6522_Write(nMB*NUM_DEVS_PER_MB + SY6522_DEVICE_A, nAddr&0xf, nValue);
	else if((nOffset >= SY6522B_Offset) && (nOffset <= (SY6522B_Offset+0x0F)))
		SY6522_Write(nMB*NUM_DEVS_PER_MB + SY6522_DEVICE_B, nAddr&0xf, nValue);
	else if((nOffset >= SSI263_Offset) && (nOffset <= (SSI263_Offset+0x05)))
#if 0 // ENABLE_SSI263
		SSI263_Write(nMB*2+1, nAddr&0xf, nValue);		// Second 6522 is used for speech chip
#else
                mb_assert(false);
#endif

	return/*0*/;
}

//-----------------------------------------------------------------------------

#if 1 // APPLE2IX
GLUE_C_READ(PhasorIO)
#else
static BYTE __stdcall PhasorIO(WORD PC, WORD nAddr, BYTE bWrite, BYTE nValue, ULONG nCyclesLeft)
#endif
{
	if(!g_bPhasorEnable)
#if 1 // APPLE2IX
		return MemReadFloatingBus();
#else
		return MemReadFloatingBus(nCyclesLeft);
#endif

	if(g_nPhasorMode < 2)
		g_nPhasorMode = nAddr & 1;

	g_PhasorClockScaleFactor = (nAddr & 4) ? 2 : 1;

#if 1 // APPLE2IX
	AY8910_InitClock((int)CLK_6502 * g_PhasorClockScaleFactor, SAMPLE_RATE);

	return MemReadFloatingBus();
#else
	AY8910_InitClock((int)(CLK_6502 * g_PhasorClockScaleFactor));

	return MemReadFloatingBus(nCyclesLeft);
#endif
}

//-----------------------------------------------------------------------------
#if 1 // APPLE2IX

#define IO_Null NULL

void mb_io_initialize(unsigned int slot4, unsigned int slot5)
{
    MB_InitializeIO(NULL, slot4, slot5);
}

//typedef uint8_t (*iofunction)(uint16_t nPC, uint16_t nAddr, uint8_t nWriteFlag, uint8_t nWriteValue, unsigned long nCyclesLeft);
typedef void (*iofunction)(void);
static void RegisterIoHandler(unsigned int uSlot, iofunction IOReadC0, iofunction IOWriteC0, iofunction IOReadCx, iofunction IOWriteCx, void *unused_lpSlotParameter, uint8_t* unused_pExpansionRom)
{

    // card softswitches
    unsigned int base_addr = 0xC080 + (uSlot<<4); // uSlot == 4 => 0xC0C0 , uSlot == 5 => 0xC0D0
    if (IOReadC0)
    {
        assert(IOWriteC0);
        for (unsigned int i = 0; i < 16; i++)
        {
            cpu65_vmem_r[base_addr+i] = IOReadC0;
            cpu65_vmem_w[base_addr+i] = IOWriteC0;
        }
    }

    // card page
    base_addr = 0xC000 + (uSlot<<8); // uSlot == 4 => 0xC400 , uSlot == 5 => 0xC500
    for (unsigned int i = 0; i < 0x100; i++)
    {
        //cpu65_vmem_r[base_addr+i] = IOReadCx; -- CANNOT DO THIS HERE -- DEPENDS ON cxrom softswitch
        cpu65_vmem_w[base_addr+i] = IOWriteCx;
    }
}
#endif

void MB_InitializeIO(char *unused_pCxRomPeripheral, unsigned int uSlot4, unsigned int uSlot5)
{
	// Mockingboard: Slot 4 & 5
	// Phasor      : Slot 4
	// <other>     : Slot 4 & 5

	if (g_Slot4 != CT_MockingboardC && g_Slot4 != CT_Phasor)
	{
		MB_SetSoundcardType(CT_Empty);
		return;
	}

	if (g_Slot4 == CT_MockingboardC)
		RegisterIoHandler(uSlot4, IO_Null, IO_Null, MB_Read, MB_Write, NULL, NULL);
	else	// Phasor
		RegisterIoHandler(uSlot4, PhasorIO, PhasorIO, MB_Read, MB_Write, NULL, NULL);

	if (g_Slot5 == CT_MockingboardC)
		RegisterIoHandler(uSlot5, IO_Null, IO_Null, MB_Read, MB_Write, NULL, NULL);

	MB_SetSoundcardType(g_Slot4);
}

//-----------------------------------------------------------------------------

void MB_Mute()
{
	if(g_SoundcardType == CT_Empty)
		return;

	if(MockingboardVoice->bActive && !MockingboardVoice->bMute)
	{
#if 0 // !APPLE2IX
		MockingboardVoice.lpDSBvoice->SetVolume(DSBVOLUME_MIN);
#endif
		MockingboardVoice->bMute = true;
	}

#if 0 // !APPLE2IX
	if(g_nCurrentActivePhoneme >= 0)
		SSI263Voice[g_nCurrentActivePhoneme].lpDSBvoice->SetVolume(DSBVOLUME_MIN);
#endif
}

//-----------------------------------------------------------------------------

void MB_Demute()
{
	if(g_SoundcardType == CT_Empty)
		return;

	if(MockingboardVoice->bActive && MockingboardVoice->bMute)
	{
#if 0 // !APPLE2IX
		MockingboardVoice.lpDSBvoice->SetVolume(MockingboardVoice.nVolume);
#endif
		MockingboardVoice->bMute = false;
	}

#if 0 // !APPLE2IX
	if(g_nCurrentActivePhoneme >= 0)
		SSI263Voice[g_nCurrentActivePhoneme].lpDSBvoice->SetVolume(SSI263Voice[g_nCurrentActivePhoneme].nVolume);
#endif
}

//-----------------------------------------------------------------------------

// Called by CpuExecute() before doing CPU emulation
void MB_StartOfCpuExecute()
{
	g_uLastCumulativeCycles = cycles_count_total;
}

// Called by ContinueExecution() at the end of every video frame
void MB_EndOfVideoFrame()
{
	if(g_SoundcardType == CT_Empty)
		return;
#if MB_TRACING
        if (mb_trace_fp) {
            fprintf(mb_trace_fp, "%s", "MB_EndOfVideoFrame\n");
        }
#endif

	if(!g_bMBTimerIrqActive)
		MB_Update();
}

//-----------------------------------------------------------------------------

// Called by CpuExecute() after every N opcodes (N = ~1000 @ 1MHz)
#if 1 // APPLE2IX
void MB_UpdateCycles(void)
#else
void MB_UpdateCycles(ULONG uExecutedCycles)
#endif
{
	if(g_SoundcardType == CT_Empty)
		return;

	timing_checkpoint_cycles();
	unsigned long uCycles = cycles_count_total - g_uLastCumulativeCycles;
	g_uLastCumulativeCycles = cycles_count_total;
#if MB_TRACING
        if (mb_trace_fp) {
            fprintf(mb_trace_fp, "\tuCycles:%lu\n", uCycles);
        }
#endif
#if 1 // APPLE2IX
        if (uCycles >= 0x10000) {
            LOG("OOPS!!! Mockingboard failed assert!");
            uCycles %= 0x10000;
        }
#else
	_ASSERT(uCycles < 0x10000);
#endif
	uint16_t nClocks = (uint16_t) uCycles;

	for(int i=0; i<NUM_SY6522; i++)
	{
		SY6522_AY8910* pMB = &g_MB[i];

		uint16_t OldTimer1 = pMB->sy6522.TIMER1_COUNTER.w;

		pMB->sy6522.TIMER1_COUNTER.w -= nClocks;
		pMB->sy6522.TIMER2_COUNTER.w -= nClocks;

		// Check for counter underflow
		bool bTimer1Underflow = (!(OldTimer1 & 0x8000) && (pMB->sy6522.TIMER1_COUNTER.w & 0x8000));

		if( bTimer1Underflow && (g_nMBTimerDevice == i) && g_bMBTimerIrqActive )
		{
#if MB_TRACING
                        if (mb_trace_fp) {
                            fprintf(mb_trace_fp, "\ttimer1 (%d) underflow\n", i);
                        }
#endif
#if 0 // _DEBUG
			g_uTimer1IrqCount++;	// DEBUG
#endif

			pMB->sy6522.IFR |= IxR_TIMER1;
			UpdateIFR(pMB);

			if((pMB->sy6522.ACR & RUNMODE) == RM_ONESHOT)
			{
				// One-shot mode
				// - Phasor's playback code uses one-shot mode
				// - Willy Byte sets to one-shot to stop the timer IRQ
#if MB_TRACING
                                if (mb_trace_fp) {
                                    fprintf(mb_trace_fp, "\tstop timer %d\n", i);
                                }
#endif
				StopTimer(pMB);
			}
			else
			{
				// Free-running mode
				// - Ultima4/5 change ACCESS_TIMER1 after a couple of IRQs into tune
				pMB->sy6522.TIMER1_COUNTER.w = pMB->sy6522.TIMER1_LATCH.w;
#if MB_TRACING
                                if (mb_trace_fp) {
                                    fprintf(mb_trace_fp, "\tstart timer %d\n", i);
                                }
#endif
				StartTimer(pMB);
			}

			MB_Update();
		}
		else if ( bTimer1Underflow
					&& !g_bMBTimerIrqActive								// StopTimer() has been called
					&& (pMB->sy6522.IFR & IxR_TIMER1)					// IRQ
					&& ((pMB->sy6522.ACR & RUNMODE) == RM_ONESHOT) )	// One-shot mode
		{
#if MB_TRACING
                        if (mb_trace_fp) {
                            fprintf(mb_trace_fp, "\ttimer1 (%d) alt underflow\n", i);
                        }
#endif
			// Fix for Willy Byte - need to confirm that 6522 really does this!
			// . It never accesses IER/IFR/TIMER1 regs to clear IRQ
			pMB->sy6522.IFR &= ~IxR_TIMER1;		// Deassert the TIMER IRQ
			UpdateIFR(pMB);
		}
	}
}

//-----------------------------------------------------------------------------

SS_CARDTYPE MB_GetSoundcardType()
{
	return g_SoundcardType;
}

void MB_SetSoundcardType(SS_CARDTYPE NewSoundcardType)
{
//	if ((NewSoundcardType == SC_UNINIT) || (g_SoundcardType == NewSoundcardType))
	if (g_SoundcardType == NewSoundcardType)
		return;

	g_SoundcardType = NewSoundcardType;

	if(g_SoundcardType == CT_Empty)
		MB_Mute();

	g_bPhasorEnable = (g_SoundcardType == CT_Phasor);
}

//-----------------------------------------------------------------------------

double MB_GetFramePeriod()
{
	return (g_bMBTimerIrqActive||(g_MB[0].sy6522.IFR & IxR_TIMER1)) ? (double)g_n6522TimerPeriod : g_f6522TimerPeriod_NoIRQ;
}

bool MB_IsActive()
{
	if(!MockingboardVoice->bActive)
		return false;

	// Ignore /g_bMBTimerIrqActive/ as timer's irq handler will access 6522 regs affecting /g_bMB_Active/

	return g_bMB_Active;
}

//-----------------------------------------------------------------------------
#if 1 // APPLE2IX
void MB_SetVolumeZeroToTen(unsigned long goesToTen) {
    samplesScale = goesToTen/10.f;
}
#else
DWORD MB_GetVolume()
{
	return MockingboardVoice.dwUserVolume;
}

void MB_SetVolume(DWORD dwVolume, DWORD dwVolumeMax)
{
	MockingboardVoice.dwUserVolume = dwVolume;

	MockingboardVoice.nVolume = NewVolume(dwVolume, dwVolumeMax);

	if(MockingboardVoice.bActive)
		MockingboardVoice.lpDSBvoice->SetVolume(MockingboardVoice.nVolume);
}
#endif

//===========================================================================

// Called by debugger - Debugger_Display.cpp
#if 0 // !APPLE2IX
void MB_GetSnapshot_v1(SS_CARD_MOCKINGBOARD_v1* const pSS, const DWORD dwSlot)
{
	pSS->Hdr.UnitHdr.hdr.v2.Length = sizeof(SS_CARD_MOCKINGBOARD_v1);
	pSS->Hdr.UnitHdr.hdr.v2.Type = UT_Card;
	pSS->Hdr.UnitHdr.hdr.v2.Version = 1;

	pSS->Hdr.Slot = dwSlot;
	pSS->Hdr.Type = CT_MockingboardC;

	UINT nMbCardNum = dwSlot - SLOT4;
	UINT nDeviceNum = nMbCardNum*2;
	SY6522_AY8910* pMB = &g_MB[nDeviceNum];

	for(UINT i=0; i<MB_UNITS_PER_CARD_v1; i++)
	{
		memcpy(&pSS->Unit[i].RegsSY6522, &pMB->sy6522, sizeof(SY6522));
		memcpy(&pSS->Unit[i].RegsAY8910, AY8910_GetRegsPtr(nDeviceNum), 16);
		memcpy(&pSS->Unit[i].RegsSSI263, &pMB->SpeechChip, sizeof(SSI263A));
		pSS->Unit[i].nAYCurrentRegister = pMB->nAYCurrentRegister;
		pSS->Unit[i].bTimer1IrqPending = false;
		pSS->Unit[i].bTimer2IrqPending = false;
		pSS->Unit[i].bSpeechIrqPending = false;

		nDeviceNum++;
		pMB++;
	}
}

int MB_SetSnapshot_v1(const SS_CARD_MOCKINGBOARD_v1* const pSS, const DWORD /*dwSlot*/)
{
	if(pSS->Hdr.UnitHdr.hdr.v1.dwVersion != MAKE_VERSION(1,0,0,0))
		return -1;

	UINT nMbCardNum = pSS->Hdr.Slot - SLOT4;
	UINT nDeviceNum = nMbCardNum*2;
	SY6522_AY8910* pMB = &g_MB[nDeviceNum];

	g_nSSI263Device = 0;
	g_nCurrentActivePhoneme = -1;

	for(UINT i=0; i<MB_UNITS_PER_CARD_v1; i++)
	{
		memcpy(&pMB->sy6522, &pSS->Unit[i].RegsSY6522, sizeof(SY6522));
		memcpy(AY8910_GetRegsPtr(nDeviceNum), &pSS->Unit[i].RegsAY8910, 16);
		memcpy(&pMB->SpeechChip, &pSS->Unit[i].RegsSSI263, sizeof(SSI263A));
		pMB->nAYCurrentRegister = pSS->Unit[i].nAYCurrentRegister;

		StartTimer(pMB);	// Attempt to start timer

		//

		// Crude - currently only support a single speech chip
		// FIX THIS:
		// . Speech chip could be Votrax instead
		// . Is this IRQ compatible with Phasor?
		if(pMB->SpeechChip.DurationPhoneme)
		{
			g_nSSI263Device = nDeviceNum;

			if((pMB->SpeechChip.CurrentMode != MODE_IRQ_DISABLED) && (pMB->sy6522.PCR == 0x0C) && (pMB->sy6522.IER & IxR_PERIPHERAL))
			{
				pMB->sy6522.IFR |= IxR_PERIPHERAL;
				UpdateIFR(pMB);
				pMB->SpeechChip.CurrentMode |= 1;	// Set SSI263's D7 pin
			}
		}

		nDeviceNum++;
		pMB++;
	}

	return 0;
}
#endif

//===========================================================================

#if 1 // APPLE2IX

static void mb_prefsChanged(const char *domain) {
    long lVal = 0;
    long goesToTen = prefs_parseLongValue(domain, PREF_MOCKINGBOARD_VOLUME, &lVal, /*base:*/10) ? lVal : 5; // expected range 0-10
    if (goesToTen < 0) {
        goesToTen = 0;
    }
    if (goesToTen > 10) {
        goesToTen = 10;
    }
    MB_SetVolumeZeroToTen(goesToTen);
}

static __attribute__((constructor)) void _init_mockingboard(void) {
    prefs_registerListener(PREF_DOMAIN_AUDIO, &mb_prefsChanged);
}

static bool _sy6522_saveState(StateHelper_s *helper, SY6522 *sy6522) {
    int fd = helper->fd;

    bool saved = false;
    do {
        uint8_t state8 = 0x0;

        state8 = sy6522->ORA;
        if (!helper->save(fd, &state8, 1)) {
            break;
        }
        state8 = sy6522->ORB;
        if (!helper->save(fd, &state8, 1)) {
            break;
        }
        state8 = sy6522->DDRA;
        if (!helper->save(fd, &state8, 1)) {
            break;
        }
        state8 = sy6522->DDRB;
        if (!helper->save(fd, &state8, 1)) {
            break;
        }

        uint8_t serialized[2] = { 0 };
        serialized[0] = (uint8_t)((sy6522->TIMER1_COUNTER.w & 0xFF00) >> 8);
        serialized[1] = (uint8_t)((sy6522->TIMER1_COUNTER.w & 0xFF  ) >> 0);
        if (!helper->save(fd, serialized, 2)) {
            break;
        }
        serialized[0] = (uint8_t)((sy6522->TIMER1_LATCH.w & 0xFF00) >> 8);
        serialized[1] = (uint8_t)((sy6522->TIMER1_LATCH.w & 0xFF  ) >> 0);
        if (!helper->save(fd, serialized, 2)) {
            break;
        }
        serialized[0] = (uint8_t)((sy6522->TIMER2_COUNTER.w & 0xFF00) >> 8);
        serialized[1] = (uint8_t)((sy6522->TIMER2_COUNTER.w & 0xFF  ) >> 0);
        if (!helper->save(fd, serialized, 2)) {
            break;
        }
        serialized[0] = (uint8_t)((sy6522->TIMER2_LATCH.w & 0xFF00) >> 8);
        serialized[1] = (uint8_t)((sy6522->TIMER2_LATCH.w & 0xFF  ) >> 0);
        if (!helper->save(fd, serialized, 2)) {
            break;
        }

        state8 = sy6522->SERIAL_SHIFT;
        if (!helper->save(fd, &state8, 1)) {
            break;
        }
        state8 = sy6522->ACR;
        if (!helper->save(fd, &state8, 1)) {
            break;
        }
        state8 = sy6522->PCR;
        if (!helper->save(fd, &state8, 1)) {
            break;
        }
        state8 = sy6522->IFR;
        if (!helper->save(fd, &state8, 1)) {
            break;
        }
        state8 = sy6522->IER;
        if (!helper->save(fd, &state8, 1)) {
            break;
        }

        // NB. No need to write ORA_NO_HS, since same data as ORA, just without handshake

        saved = true;
    } while (0);

    return saved;
}

static bool _sy6522_loadState(StateHelper_s *helper, SY6522 *sy6522) {
    int fd = helper->fd;

    bool loaded = false;
    do {
        if (!helper->load(fd, &(sy6522->ORA), 1)) {
            break;
        }
        if (!helper->load(fd, &(sy6522->ORB), 1)) {
            break;
        }
        if (!helper->load(fd, &(sy6522->DDRA), 1)) {
            break;
        }
        if (!helper->load(fd, &(sy6522->DDRB), 1)) {
            break;
        }

        uint8_t serialized[2] = { 0 };

        if (!helper->load(fd, serialized, 2)) {
            break;
        }
        sy6522->TIMER1_COUNTER.h = serialized[0];
        sy6522->TIMER1_COUNTER.l = serialized[1];

        if (!helper->load(fd, serialized, 2)) {
            break;
        }
        sy6522->TIMER1_LATCH.h = serialized[0];
        sy6522->TIMER1_LATCH.l = serialized[1];

        if (!helper->load(fd, serialized, 2)) {
            break;
        }
        sy6522->TIMER2_COUNTER.h = serialized[0];
        sy6522->TIMER2_COUNTER.l = serialized[1];

        if (!helper->load(fd, serialized, 2)) {
            break;
        }
        sy6522->TIMER2_LATCH.h = serialized[0];
        sy6522->TIMER2_LATCH.l = serialized[1];

        if (!helper->load(fd, &(sy6522->SERIAL_SHIFT), 1)) {
            break;
        }
        if (!helper->load(fd, &(sy6522->ACR), 1)) {
            break;
        }
        if (!helper->load(fd, &(sy6522->PCR), 1)) {
            break;
        }
        if (!helper->load(fd, &(sy6522->IFR), 1)) {
            break;
        }
        if (!helper->load(fd, &(sy6522->IER), 1)) {
            break;
        }

        // NB. No need to write ORA_NO_HS, since same data as ORA, just without handshake

        loaded = true;
    } while (0);

    return loaded;
}

static bool _ssi263_saveState(StateHelper_s *helper, SSI263A *ssi263) {
    int fd = helper->fd;

    bool saved = false;
    do {
        if (!helper->save(fd, &(ssi263->DurationPhoneme), 1)) {
            break;
        }
        if (!helper->save(fd, &(ssi263->Inflection), 1)) {
            break;
        }
        if (!helper->save(fd, &(ssi263->RateInflection), 1)) {
            break;
        }
        if (!helper->save(fd, &(ssi263->CtrlArtAmp), 1)) {
            break;
        }
        if (!helper->save(fd, &(ssi263->FilterFreq), 1)) {
            break;
        }
        if (!helper->save(fd, &(ssi263->CurrentMode), 1)) {
            break;
        }

        saved = true;
    } while (0);

    return saved;
}

static bool _ssi263_loadState(StateHelper_s *helper, SSI263A *ssi263) {
    int fd = helper->fd;

    bool loaded = false;
    do {
        if (!helper->load(fd, &(ssi263->DurationPhoneme), 1)) {
            break;
        }
        if (!helper->load(fd, &(ssi263->Inflection), 1)) {
            break;
        }
        if (!helper->load(fd, &(ssi263->RateInflection), 1)) {
            break;
        }
        if (!helper->load(fd, &(ssi263->CtrlArtAmp), 1)) {
            break;
        }
        if (!helper->load(fd, &(ssi263->FilterFreq), 1)) {
            break;
        }
        if (!helper->load(fd, &(ssi263->CurrentMode), 1)) {
            break;
        }

        loaded = true;
    } while (0);

    return loaded;
}

bool mb_saveState(StateHelper_s *helper) {
    LOG("SAVE mockingboard state ...");
    int fd = helper->fd;

    bool saved = false;
    for (unsigned int i=0; i<NUM_DEVS_PER_MB; i++) {

        unsigned int deviceIdx = i<<1;
        SY6522_AY8910 *mb = &g_MB[deviceIdx];

        for (unsigned int j=0; j<NUM_MB; j++) {

            if (!_sy6522_saveState(helper, &(mb->sy6522))) {
                goto exit_save;
            }
            if (!_ay8910_saveState(helper, deviceIdx)) {
                goto exit_save;
            }
            if (!_ssi263_saveState(helper, &(mb->SpeechChip))) {
                goto exit_save;
            }

            if (!helper->save(fd, &(mb->nAYCurrentRegister), 1)) {
                goto exit_save;
            }

            // TIMER1 IRQ
            // TIMER2 IRQ
            // SPEECH IRQ

            deviceIdx++;
            mb++;
        }
    }
    saved = true;

exit_save:
    return saved;
}

bool mb_loadState(StateHelper_s *helper) {
    LOG("LOAD mockingboard state ...");
    int fd = helper->fd;

    // NOTE : always load state and calculate based on CPU @1.0 scale
    double cpuScaleFactor = cpu_scale_factor;
    double cpuAltScaleFactor = cpu_altscale_factor;
    cpu_scale_factor = 1.;
    cpu_altscale_factor = 1.;
    timing_initialize();

    MB_Reset();
    AY8910UpdateSetCycles();

    bool loaded = false;
    for (unsigned int i=0; i<NUM_DEVS_PER_MB; i++) {

        for (unsigned int j=0; j<NUM_MB; j++) {

            unsigned int idx = (i<<1) + j;
            SY6522_AY8910 *mb = &g_MB[idx];

            if (!_sy6522_loadState(helper, &(mb->sy6522))) {
                LOG("could not load SY6522 %u %u", i, j);
                goto exit_load;
            }
            if (!_ay8910_loadState(helper, idx)) {
                LOG("could not load AY8910 %u %u", i, j);
                goto exit_load;
            }
            if (!_ssi263_loadState(helper, &(mb->SpeechChip))) {
                LOG("could not load SSI263 %u %u", i, j);
                goto exit_load;
            }

            if (!helper->load(fd, &(mb->nAYCurrentRegister), 1)) {
                LOG("could not load nAYCurrentRegister %u %u", i, j);
                goto exit_load;
            }

            // TIMER1 IRQ
            // TIMER2 IRQ
            // SPEECH IRQ

            StartTimer(mb);

            ++mb;
        }
    }
    loaded = true;

    MB_Reinitialize();

exit_load:

    cpu_scale_factor = cpuScaleFactor;
    cpu_altscale_factor = cpuAltScaleFactor;
    timing_initialize();

    return loaded;
}

#   if TESTING
static int _assert_testData16(const uint16_t data16, uint8_t **exData) {
    uint8_t *expected = *exData;
    uint16_t d16 = (uint16_t)(expected[0] << 8) |
                   (uint16_t)(expected[1] << 0);
    ASSERT(d16 == data16);
    *exData += 2;
    PASS();
}

static int _sy6522_testAssertA2V2(SY6522 *sy6522, uint8_t **exData) {

    uint8_t *expected = *exData;

    ASSERT(sy6522->ORA == *expected++);
    ASSERT(sy6522->ORB == *expected++);
    ASSERT(sy6522->DDRA == *expected++);
    ASSERT(sy6522->DDRB == *expected++);

    _assert_testData16(sy6522->TIMER1_COUNTER.w, &expected);
    _assert_testData16(sy6522->TIMER1_LATCH.w, &expected);
    _assert_testData16(sy6522->TIMER2_COUNTER.w, &expected);
    _assert_testData16(sy6522->TIMER2_LATCH.w, &expected);

    ASSERT(sy6522->SERIAL_SHIFT == *expected++);
    ASSERT(sy6522->ACR == *expected++);
    ASSERT(sy6522->PCR == *expected++);
    ASSERT(sy6522->IFR == *expected++);
    ASSERT(sy6522->IER == *expected++);

    *exData = expected;

    PASS();
}

static int _ssi263_testAssertA2V2(SSI263A *ssi263, uint8_t **exData) {

    uint8_t *expected = *exData;

    ASSERT(ssi263->DurationPhoneme == *expected++);
    ASSERT(ssi263->Inflection == *expected++);
    ASSERT(ssi263->RateInflection == *expected++);
    ASSERT(ssi263->CtrlArtAmp == *expected++);
    ASSERT(ssi263->FilterFreq == *expected++);
    ASSERT(ssi263->CurrentMode == *expected++);

    *exData = expected;

    PASS();
}

int mb_testAssertA2V2(uint8_t *exData, size_t dataSiz) {

    uint8_t *exStart = exData;

    for (unsigned int i=0; i<NUM_DEVS_PER_MB; i++) {
        for (unsigned int j=0; j<NUM_MB; j++) {
            unsigned int idx = (i<<1) + j;
            SY6522_AY8910 *mb = &g_MB[idx];

            _sy6522_testAssertA2V2(&(mb->sy6522), &exData);
            _ay8910_testAssertA2V2(idx, &exData);
            _ssi263_testAssertA2V2(&(mb->SpeechChip), &exData);

            ASSERT(mb->nAYCurrentRegister == *exData);
            ++exData;

            // TIMER1 IRQ
            // TIMER2 IRQ
            // SPEECH IRQ

            ++mb;
        }
    }

    ASSERT(exData - exStart == dataSiz);

    PASS();
}
#   endif // TESTING

#else // !APPLE2IX

static UINT DoWriteFile(const HANDLE hFile, const void* const pData, const UINT Length)
{
	DWORD dwBytesWritten;
	BOOL bRes = WriteFile(	hFile,
							pData,
							Length,
							&dwBytesWritten,
							NULL);

	if(!bRes || (dwBytesWritten != Length))
	{
		//dwError = GetLastError();
		throw std::string("Card: save error");
	}

	return dwBytesWritten;
}

static UINT DoReadFile(const HANDLE hFile, void* const pData, const UINT Length)
{
	DWORD dwBytesRead;
	BOOL bRes = ReadFile(	hFile,
							pData,
							Length,
							&dwBytesRead,
							NULL);

	if (dwBytesRead != Length)
		throw std::string("Card: file corrupt");

	return dwBytesRead;
}

//===========================================================================

const UINT NUM_MB_UNITS = 2;
const UINT NUM_PHASOR_UNITS = 2;

#define SS_YAML_KEY_MB_UNIT "Unit"
#define SS_YAML_KEY_SY6522 "SY6522"
#define SS_YAML_KEY_SY6522_REG_ORB "ORB"
#define SS_YAML_KEY_SY6522_REG_ORA "ORA"
#define SS_YAML_KEY_SY6522_REG_DDRB "DDRB"
#define SS_YAML_KEY_SY6522_REG_DDRA "DDRA"
#define SS_YAML_KEY_SY6522_REG_T1_COUNTER "Timer1 Counter"
#define SS_YAML_KEY_SY6522_REG_T1_LATCH "Timer1 Latch"
#define SS_YAML_KEY_SY6522_REG_T2_COUNTER "Timer2 Counter"
#define SS_YAML_KEY_SY6522_REG_T2_LATCH "Timer2 Latch"
#define SS_YAML_KEY_SY6522_REG_SERIAL_SHIFT "Serial Shift"
#define SS_YAML_KEY_SY6522_REG_ACR "ACR"
#define SS_YAML_KEY_SY6522_REG_PCR "PCR"
#define SS_YAML_KEY_SY6522_REG_IFR "IFR"
#define SS_YAML_KEY_SY6522_REG_IER "IER"
#define SS_YAML_KEY_SSI263 "SSI263"
#define SS_YAML_KEY_SSI263_REG_DUR_PHON "Duration / Phoneme"
#define SS_YAML_KEY_SSI263_REG_INF "Inflection"
#define SS_YAML_KEY_SSI263_REG_RATE_INF "Rate / Inflection"
#define SS_YAML_KEY_SSI263_REG_CTRL_ART_AMP "Control / Articulation / Amplitude"
#define SS_YAML_KEY_SSI263_REG_FILTER_FREQ "Filter Frequency"
#define SS_YAML_KEY_SSI263_REG_CURRENT_MODE "Current Mode"
#define SS_YAML_KEY_AY_CURR_REG "AY Current Register"
#define SS_YAML_KEY_TIMER1_IRQ "Timer1 IRQ Pending"
#define SS_YAML_KEY_TIMER2_IRQ "Timer2 IRQ Pending"
#define SS_YAML_KEY_SPEECH_IRQ "Speech IRQ Pending"

#define SS_YAML_KEY_PHASOR_UNIT "Unit"
#define SS_YAML_KEY_PHASOR_CLOCK_SCALE_FACTOR "Clock Scale Factor"
#define SS_YAML_KEY_PHASOR_MODE "Mode"

std::string MB_GetSnapshotCardName(void)
{
	static const std::string name("Mockingboard C");
	return name;
}

std::string Phasor_GetSnapshotCardName(void)
{
	static const std::string name("Phasor");
	return name;
}

static void SaveSnapshotSY6522(YamlSaveHelper& yamlSaveHelper, SY6522& sy6522)
{
	YamlSaveHelper::Label label(yamlSaveHelper, "%s:\n", SS_YAML_KEY_SY6522);

	yamlSaveHelper.SaveHexUint8(SS_YAML_KEY_SY6522_REG_ORB, sy6522.ORB);
	yamlSaveHelper.SaveHexUint8(SS_YAML_KEY_SY6522_REG_ORA, sy6522.ORA);
	yamlSaveHelper.SaveHexUint8(SS_YAML_KEY_SY6522_REG_DDRB, sy6522.DDRB);
	yamlSaveHelper.SaveHexUint8(SS_YAML_KEY_SY6522_REG_DDRA, sy6522.DDRA);
	yamlSaveHelper.SaveHexUint16(SS_YAML_KEY_SY6522_REG_T1_COUNTER, sy6522.TIMER1_COUNTER.w);
	yamlSaveHelper.SaveHexUint16(SS_YAML_KEY_SY6522_REG_T1_LATCH,   sy6522.TIMER1_LATCH.w);
	yamlSaveHelper.SaveHexUint16(SS_YAML_KEY_SY6522_REG_T2_COUNTER, sy6522.TIMER2_COUNTER.w);
	yamlSaveHelper.SaveHexUint16(SS_YAML_KEY_SY6522_REG_T2_LATCH,   sy6522.TIMER2_LATCH.w);
	yamlSaveHelper.SaveHexUint8(SS_YAML_KEY_SY6522_REG_SERIAL_SHIFT, sy6522.SERIAL_SHIFT);
	yamlSaveHelper.SaveHexUint8(SS_YAML_KEY_SY6522_REG_ACR, sy6522.ACR);
	yamlSaveHelper.SaveHexUint8(SS_YAML_KEY_SY6522_REG_PCR, sy6522.PCR);
	yamlSaveHelper.SaveHexUint8(SS_YAML_KEY_SY6522_REG_IFR, sy6522.IFR);
	yamlSaveHelper.SaveHexUint8(SS_YAML_KEY_SY6522_REG_IER, sy6522.IER);
	// NB. No need to write ORA_NO_HS, since same data as ORA, just without handshake
}

static void SaveSnapshotSSI263(YamlSaveHelper& yamlSaveHelper, SSI263A& ssi263)
{
	YamlSaveHelper::Label label(yamlSaveHelper, "%s:\n", SS_YAML_KEY_SSI263);

	yamlSaveHelper.SaveHexUint8(SS_YAML_KEY_SSI263_REG_DUR_PHON, ssi263.DurationPhoneme);
	yamlSaveHelper.SaveHexUint8(SS_YAML_KEY_SSI263_REG_INF, ssi263.Inflection);
	yamlSaveHelper.SaveHexUint8(SS_YAML_KEY_SSI263_REG_RATE_INF, ssi263.RateInflection);
	yamlSaveHelper.SaveHexUint8(SS_YAML_KEY_SSI263_REG_CTRL_ART_AMP, ssi263.CtrlArtAmp);
	yamlSaveHelper.SaveHexUint8(SS_YAML_KEY_SSI263_REG_FILTER_FREQ, ssi263.FilterFreq);
	yamlSaveHelper.SaveHexUint8(SS_YAML_KEY_SSI263_REG_CURRENT_MODE, ssi263.CurrentMode);
}

void MB_SaveSnapshot(YamlSaveHelper& yamlSaveHelper, const UINT uSlot)
{
	const UINT nMbCardNum = uSlot - SLOT4;
	UINT nDeviceNum = nMbCardNum*2;
	SY6522_AY8910* pMB = &g_MB[nDeviceNum];

	YamlSaveHelper::Slot slot(yamlSaveHelper, MB_GetSnapshotCardName(), uSlot, 1);	// fixme: object should be just 1 Mockingboard card & it will know its slot

	YamlSaveHelper::Label state(yamlSaveHelper, "%s:\n", SS_YAML_KEY_STATE);

	for(UINT i=0; i<NUM_MB_UNITS; i++)
	{
		YamlSaveHelper::Label unit(yamlSaveHelper, "%s%d:\n", SS_YAML_KEY_MB_UNIT, i);

		SaveSnapshotSY6522(yamlSaveHelper, pMB->sy6522);
		AY8910_SaveSnapshot(yamlSaveHelper, nDeviceNum, std::string(""));
		SaveSnapshotSSI263(yamlSaveHelper, pMB->SpeechChip);

		yamlSaveHelper.SaveHexUint4(SS_YAML_KEY_AY_CURR_REG, pMB->nAYCurrentRegister);
		yamlSaveHelper.Save("%s: %s # Not supported\n", SS_YAML_KEY_TIMER1_IRQ, "false");
		yamlSaveHelper.Save("%s: %s # Not supported\n", SS_YAML_KEY_TIMER2_IRQ, "false");
		yamlSaveHelper.Save("%s: %s # Not supported\n", SS_YAML_KEY_SPEECH_IRQ, "false");

		nDeviceNum++;
		pMB++;
	}
}

static void LoadSnapshotSY6522(YamlLoadHelper& yamlLoadHelper, SY6522& sy6522)
{
	if (!yamlLoadHelper.GetSubMap(SS_YAML_KEY_SY6522))
		throw std::string("Card: Expected key: ") + std::string(SS_YAML_KEY_SY6522);

	sy6522.ORB  = yamlLoadHelper.LoadUint(SS_YAML_KEY_SY6522_REG_ORB);
	sy6522.ORA  = yamlLoadHelper.LoadUint(SS_YAML_KEY_SY6522_REG_ORA);
	sy6522.DDRB = yamlLoadHelper.LoadUint(SS_YAML_KEY_SY6522_REG_DDRB);
	sy6522.DDRA = yamlLoadHelper.LoadUint(SS_YAML_KEY_SY6522_REG_DDRA);
	sy6522.TIMER1_COUNTER.w = yamlLoadHelper.LoadUint(SS_YAML_KEY_SY6522_REG_T1_COUNTER);
	sy6522.TIMER1_LATCH.w   = yamlLoadHelper.LoadUint(SS_YAML_KEY_SY6522_REG_T1_LATCH);
	sy6522.TIMER2_COUNTER.w = yamlLoadHelper.LoadUint(SS_YAML_KEY_SY6522_REG_T2_COUNTER);
	sy6522.TIMER2_LATCH.w   = yamlLoadHelper.LoadUint(SS_YAML_KEY_SY6522_REG_T2_LATCH);
	sy6522.SERIAL_SHIFT     = yamlLoadHelper.LoadUint(SS_YAML_KEY_SY6522_REG_SERIAL_SHIFT);
	sy6522.ACR  = yamlLoadHelper.LoadUint(SS_YAML_KEY_SY6522_REG_ACR);
	sy6522.PCR  = yamlLoadHelper.LoadUint(SS_YAML_KEY_SY6522_REG_PCR);
	sy6522.IFR  = yamlLoadHelper.LoadUint(SS_YAML_KEY_SY6522_REG_IFR);
	sy6522.IER  = yamlLoadHelper.LoadUint(SS_YAML_KEY_SY6522_REG_IER);
	sy6522.ORA_NO_HS = 0;	// Not saved

	yamlLoadHelper.PopMap();
}

static void LoadSnapshotSSI263(YamlLoadHelper& yamlLoadHelper, SSI263A& ssi263)
{
	if (!yamlLoadHelper.GetSubMap(SS_YAML_KEY_SSI263))
		throw std::string("Card: Expected key: ") + std::string(SS_YAML_KEY_SSI263);

	ssi263.DurationPhoneme = yamlLoadHelper.LoadUint(SS_YAML_KEY_SSI263_REG_DUR_PHON);
	ssi263.Inflection      = yamlLoadHelper.LoadUint(SS_YAML_KEY_SSI263_REG_INF);
	ssi263.RateInflection  = yamlLoadHelper.LoadUint(SS_YAML_KEY_SSI263_REG_RATE_INF);
	ssi263.CtrlArtAmp      = yamlLoadHelper.LoadUint(SS_YAML_KEY_SSI263_REG_CTRL_ART_AMP);
	ssi263.FilterFreq      = yamlLoadHelper.LoadUint(SS_YAML_KEY_SSI263_REG_FILTER_FREQ);
	ssi263.CurrentMode     = yamlLoadHelper.LoadUint(SS_YAML_KEY_SSI263_REG_CURRENT_MODE);

	yamlLoadHelper.PopMap();
}

bool MB_LoadSnapshot(YamlLoadHelper& yamlLoadHelper, UINT slot, UINT version)
{
	if (slot != 4 && slot != 5)	// fixme
		throw std::string("Card: wrong slot");

	if (version != 1)
		throw std::string("Card: wrong version");

	AY8910UpdateSetCycles();

	const UINT nMbCardNum = slot - SLOT4;
	UINT nDeviceNum = nMbCardNum*2;
	SY6522_AY8910* pMB = &g_MB[nDeviceNum];

	g_nSSI263Device = 0;
	g_nCurrentActivePhoneme = -1;

	for(UINT i=0; i<NUM_MB_UNITS; i++)
	{
		char szNum[2] = {'0'+i,0};
		std::string unit = std::string(SS_YAML_KEY_MB_UNIT) + std::string(szNum);
		if (!yamlLoadHelper.GetSubMap(unit))
			throw std::string("Card: Expected key: ") + std::string(unit);

		LoadSnapshotSY6522(yamlLoadHelper, pMB->sy6522);
		AY8910_LoadSnapshot(yamlLoadHelper, nDeviceNum, std::string(""));
		LoadSnapshotSSI263(yamlLoadHelper, pMB->SpeechChip);

		pMB->nAYCurrentRegister = yamlLoadHelper.LoadUint(SS_YAML_KEY_AY_CURR_REG);
		yamlLoadHelper.LoadBool(SS_YAML_KEY_TIMER1_IRQ);	// Consume
		yamlLoadHelper.LoadBool(SS_YAML_KEY_TIMER2_IRQ);	// Consume
		yamlLoadHelper.LoadBool(SS_YAML_KEY_SPEECH_IRQ);	// Consume

		yamlLoadHelper.PopMap();

		//

		StartTimer(pMB);	// Attempt to start timer

		// Crude - currently only support a single speech chip
		// FIX THIS:
		// . Speech chip could be Votrax instead
		// . Is this IRQ compatible with Phasor?
		if(pMB->SpeechChip.DurationPhoneme)
		{
			g_nSSI263Device = nDeviceNum;

			if((pMB->SpeechChip.CurrentMode != MODE_IRQ_DISABLED) && (pMB->sy6522.PCR == 0x0C) && (pMB->sy6522.IER & IxR_PERIPHERAL))
			{
				pMB->sy6522.IFR |= IxR_PERIPHERAL;
				UpdateIFR(pMB);
				pMB->SpeechChip.CurrentMode |= 1;	// Set SSI263's D7 pin
			}
		}

		nDeviceNum++;
		pMB++;
	}

	AY8910_InitClock((int)CLK_6502);

	// Setup in MB_InitializeIO() -> MB_SetSoundcardType()
	g_SoundcardType = CT_Empty;
	g_bPhasorEnable = false;

	return true;
}

void Phasor_SaveSnapshot(YamlSaveHelper& yamlSaveHelper, const UINT uSlot)
{
	if (uSlot != 4)
		throw std::string("Card: Phasor only supported in slot-4");

	UINT nDeviceNum = 0;
	SY6522_AY8910* pMB = &g_MB[0];	// fixme: Phasor uses MB's slot4(2x6522), slot4(2xSSI263), but slot4+5(4xAY8910)

	YamlSaveHelper::Slot slot(yamlSaveHelper, Phasor_GetSnapshotCardName(), uSlot, 1);	// fixme: object should be just 1 Mockingboard card & it will know its slot

	YamlSaveHelper::Label state(yamlSaveHelper, "%s:\n", SS_YAML_KEY_STATE);

	yamlSaveHelper.SaveUint(SS_YAML_KEY_PHASOR_CLOCK_SCALE_FACTOR, g_PhasorClockScaleFactor);
	yamlSaveHelper.SaveUint(SS_YAML_KEY_PHASOR_MODE, g_nPhasorMode);

	for(UINT i=0; i<NUM_PHASOR_UNITS; i++)
	{
		YamlSaveHelper::Label unit(yamlSaveHelper, "%s%d:\n", SS_YAML_KEY_PHASOR_UNIT, i);

		SaveSnapshotSY6522(yamlSaveHelper, pMB->sy6522);
		AY8910_SaveSnapshot(yamlSaveHelper, nDeviceNum+0, std::string("-A"));
		AY8910_SaveSnapshot(yamlSaveHelper, nDeviceNum+1, std::string("-B"));
		SaveSnapshotSSI263(yamlSaveHelper, pMB->SpeechChip);

		yamlSaveHelper.SaveHexUint4(SS_YAML_KEY_AY_CURR_REG, pMB->nAYCurrentRegister);
		yamlSaveHelper.Save("%s: %s # Not supported\n", SS_YAML_KEY_TIMER1_IRQ, "false");
		yamlSaveHelper.Save("%s: %s # Not supported\n", SS_YAML_KEY_TIMER2_IRQ, "false");
		yamlSaveHelper.Save("%s: %s # Not supported\n", SS_YAML_KEY_SPEECH_IRQ, "false");

		nDeviceNum += 2;
		pMB++;
	}
}

bool Phasor_LoadSnapshot(YamlLoadHelper& yamlLoadHelper, UINT slot, UINT version)
{
	if (slot != 4)	// fixme
		throw std::string("Card: wrong slot");

	if (version != 1)
		throw std::string("Card: wrong version");

	g_PhasorClockScaleFactor = yamlLoadHelper.LoadUint(SS_YAML_KEY_PHASOR_CLOCK_SCALE_FACTOR);
	g_nPhasorMode = yamlLoadHelper.LoadUint(SS_YAML_KEY_PHASOR_MODE);

	AY8910UpdateSetCycles();

	UINT nDeviceNum = 0;
	SY6522_AY8910* pMB = &g_MB[0];

	g_nSSI263Device = 0;
	g_nCurrentActivePhoneme = -1;

	for(UINT i=0; i<NUM_PHASOR_UNITS; i++)
	{
		char szNum[2] = {'0'+i,0};
		std::string unit = std::string(SS_YAML_KEY_MB_UNIT) + std::string(szNum);
		if (!yamlLoadHelper.GetSubMap(unit))
			throw std::string("Card: Expected key: ") + std::string(unit);

		LoadSnapshotSY6522(yamlLoadHelper, pMB->sy6522);
		AY8910_LoadSnapshot(yamlLoadHelper, nDeviceNum+0, std::string("-A"));
		AY8910_LoadSnapshot(yamlLoadHelper, nDeviceNum+1, std::string("-B"));
		LoadSnapshotSSI263(yamlLoadHelper, pMB->SpeechChip);

		pMB->nAYCurrentRegister = yamlLoadHelper.LoadUint(SS_YAML_KEY_AY_CURR_REG);
		yamlLoadHelper.LoadBool(SS_YAML_KEY_TIMER1_IRQ);	// Consume
		yamlLoadHelper.LoadBool(SS_YAML_KEY_TIMER2_IRQ);	// Consume
		yamlLoadHelper.LoadBool(SS_YAML_KEY_SPEECH_IRQ);	// Consume

		yamlLoadHelper.PopMap();

		//

		StartTimer(pMB);	// Attempt to start timer

		// Crude - currently only support a single speech chip
		// FIX THIS:
		// . Speech chip could be Votrax instead
		// . Is this IRQ compatible with Phasor?
		if(pMB->SpeechChip.DurationPhoneme)
		{
			g_nSSI263Device = nDeviceNum;

			if((pMB->SpeechChip.CurrentMode != MODE_IRQ_DISABLED) && (pMB->sy6522.PCR == 0x0C) && (pMB->sy6522.IER & IxR_PERIPHERAL))
			{
				pMB->sy6522.IFR |= IxR_PERIPHERAL;
				UpdateIFR(pMB);
				pMB->SpeechChip.CurrentMode |= 1;	// Set SSI263's D7 pin
			}
		}

		nDeviceNum += 2;
		pMB++;
	}

	AY8910_InitClock((int)(CLK_6502 * g_PhasorClockScaleFactor));

	// Setup in MB_InitializeIO() -> MB_SetSoundcardType()
	g_SoundcardType = CT_Empty;
	g_bPhasorEnable = false;

	return true;
}
#endif // !APPLE2IX

//-----------------------------------------------------------------------------

#if MB_TRACING
void mb_traceBegin(const char *trace_file) {
    if (trace_file) {
        mb_trace_fp = fopen(trace_file, "w");
        char *samp_file = NULL;
        ASPRINTF(&samp_file, "%s.samp", trace_file);
        assert(samp_file);
        mb_trace_samples_fp = fopen(samp_file, "w");
        FREE(samp_file);
    }
}

void mb_traceFlush(void) {
    if (mb_trace_fp) {
        fflush(mb_trace_fp);
    }
    if (mb_trace_samples_fp) {
        fflush(mb_trace_samples_fp);
    }
}

void mb_traceEnd(void) {
    mb_traceFlush();
    if (mb_trace_fp) {
        fclose(mb_trace_fp);
        mb_trace_fp = NULL;
    }
    if (mb_trace_samples_fp) {
        fclose(mb_trace_samples_fp);
        mb_trace_samples_fp = NULL;
    }
}
#endif
