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

/* sound.c: Sound support
   Copyright (c) 2000-2007 Russell Marks, Matan Ziv-Av, Philip Kendall,
                           Fredrick Meunier

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

   Author contact information:

   E-mail: philip-fuse@shadowmagic.org.uk

*/

// [AppleWin-TC] From FUSE's sound.c module

#if 0 // !defined(APPLE2IX)
#include "StdAfx.h"

#include <windows.h>
#include <stdio.h>
#include <crtdbg.h>
#include "AY8910.h"

#include "Applewin.h"		// For g_fh
#include "Mockingboard.h"	// For g_uTimer1IrqCount
#include "YamlHelper.h"
#else
#   include "common.h"
#   include "audio/AY8910.h"
#   include "audio/mockingboard.h"	// For g_uTimer1IrqCount
#   if TESTING
#       include "greatest.h"
#   endif
#endif

/* The AY white noise RNG algorithm is based on info from MAME's ay8910.c -
 * MAME's licence explicitly permits free use of info (even encourages it).
 */

/* NB: I know some of this stuff looks fairly CPU-hogging.
 * For example, the AY code tracks changes with sub-frame timing
 * in a rather hairy way, and there's subsampling here and there.
 * But if you measure the CPU use, it doesn't actually seem
 * very high at all. And I speak as a Cyrix owner. :-)
 */

libspectrum_signed_word** g_ppSoundBuffers;	// Used to pass param to sound_ay_overlay()

/* configuration */
//int sound_enabled = 0;		/* Are we currently using the sound card */
//int sound_enabled_ever = 0;	/* if it's *ever* been in use; see
//				   sound_ay_write() and sound_ay_reset() */
//int sound_stereo = 0;		/* true for stereo *output sample* (only) */
//int sound_stereo_ay_abc = 0;	/* (AY stereo) true for ABC stereo, else ACB */
//int sound_stereo_ay_narrow = 0;	/* (AY stereo) true for narrow AY st. sep. */

//int sound_stereo_ay = 0;	/* local copy of settings_current.stereo_ay */
//int sound_stereo_beeper = 0;	/* and settings_current.stereo_beeper */


/* assume all three tone channels together match the beeper volume (ish).
 * Must be <=127 for all channels; 50+2+(24*3) = 124.
 * (Now scaled up for 16-bit.)
 */
//#define AMPL_BEEPER		( 50 * 256)
//#define AMPL_TAPE		( 2 * 256 )
//#define AMPL_AY_TONE		( 24 * 256 )	/* three of these */
#define AMPL_AY_TONE		( 42 * 256 )	// 42*3 = 126

/* max. number of sub-frame AY port writes allowed;
 * given the number of port writes theoretically possible in a
 * 50th I think this should be plenty.
 */
//#define AY_CHANGE_MAX		8000	// [TC] Moved into AY8910.h

///* frequency to generate sound at for hifi sound */
//#define HIFI_FREQ              88200

#ifdef HAVE_SAMPLERATE
static SRC_STATE *src_state;
#endif /* #ifdef HAVE_SAMPLERATE */

int sound_generator_framesiz;
int sound_framesiz;

static int sound_generator_freq;

static int sound_channels;

static unsigned int ay_tone_levels[16];

//static libspectrum_signed_word *sound_buf, *tape_buf;
//static float *convert_input_buffer, *convert_output_buffer;

#if 0
/* beeper stuff */
static int sound_oldpos[2], sound_fillpos[2];
static int sound_oldval[2], sound_oldval_orig[2];
#endif

#if 0
#define STEREO_BUF_SIZE 4096

static int pstereobuf[ STEREO_BUF_SIZE ];
static int pstereobufsiz, pstereopos;
static int psgap = 250;
static int rstereobuf_l[ STEREO_BUF_SIZE ], rstereobuf_r[ STEREO_BUF_SIZE ];
static int rstereopos, rchan1pos, rchan2pos, rchan3pos;
#endif


// Statics:
double m_fCurrentCLK_AY8910 = 0.0;

#ifndef APPLE2IX
static void sound_end(CAY8910 *_this);
#endif
static void sound_ay_overlay(CAY8910 *_this);

void CAY8910_init(CAY8910 *_this)
{
	// Init the statics that were in sound_ay_overlay()
	_this->rng = 1;
	_this->noise_toggle = 0;
	_this->env_first = 1; _this->env_rev = 0; _this->env_counter = 15;
	//m_fCurrentCLK_AY8910 = cycles_persec_target; -- believe this is handled by an initial call to SetCLK()
};


void sound_ay_init( CAY8910 *_this )
{
	/* AY output doesn't match the claimed levels; these levels are based
	* on the measurements posted to comp.sys.sinclair in Dec 2001 by
	* Matthew Westcott, adjusted as I described in a followup to his post,
	* then scaled to 0..0xffff.
	*/
	static const int levels[16] = {
		0x0000, 0x0385, 0x053D, 0x0770,
		0x0AD7, 0x0FD5, 0x15B0, 0x230C,
		0x2B4C, 0x43C1, 0x5A4B, 0x732F,
		0x9204, 0xAFF1, 0xD921, 0xFFFF
	};
	int f;

	/* scale the values down to fit */
	for( f = 0; f < 16; f++ )
		ay_tone_levels[f] = ( levels[f] * AMPL_AY_TONE + 0x8000 ) / 0xffff;

	_this->ay_noise_tick = _this->ay_noise_period = 0;
	_this->ay_env_internal_tick = _this->ay_env_tick = _this->ay_env_period = 0;
	_this->ay_tone_subcycles = _this->ay_env_subcycles = 0;
	for( f = 0; f < 3; f++ )
		_this->ay_tone_tick[f] = _this->ay_tone_high[f] = 0, _this->ay_tone_period[f] = 1;

	_this->ay_change_count = 0;
}

#ifdef APPLE2IX
#define HZ_COMMON_DENOMINATOR 25 // FIXME TODO : why different than upstream?
#endif

static void sound_init( CAY8910 *_this, const char *device, unsigned long nSampleRate )
{
//  static int first_init = 1;
//  int f, ret;
  float hz;
#ifdef HAVE_SAMPLERATE
  int error;
#endif /* #ifdef HAVE_SAMPLERATE */

/* if we don't have any sound I/O code compiled in, don't do sound */
#ifdef NO_SOUND
  return;
#endif

#if 0
  if( !( !sound_enabled && settings_current.sound &&
	 settings_current.emulation_speed == 100 ) )
    return;

  sound_stereo_ay = settings_current.stereo_ay;
  sound_stereo_beeper = settings_current.stereo_beeper;

/* only try for stereo if we need it */
  if( sound_stereo_ay || sound_stereo_beeper )
    sound_stereo = 1;
  ret =
    sound_lowlevel_init( device, &settings_current.sound_freq,
			 &sound_stereo );
  if( ret )
    return;
#endif

#if 0
/* important to override these settings if not using stereo
 * (it would probably be confusing to mess with the stereo
 * settings in settings_current though, which is why we make copies
 * rather than using the real ones).
 */
  if( !sound_stereo ) {
    sound_stereo_ay = 0;
    sound_stereo_beeper = 0;
  }

  sound_enabled = sound_enabled_ever = 1;

  sound_channels = ( sound_stereo ? 2 : 1 );
#endif
  sound_channels = 3;	// 3 mono channels: ABC

//  hz = ( float ) machine_current->timings.processor_speed /
//    machine_current->timings.tstates_per_frame;
  hz = HZ_COMMON_DENOMINATOR;

//  sound_generator_freq =
//    settings_current.sound_hifi ? HIFI_FREQ : settings_current.sound_freq;
  sound_generator_freq = nSampleRate;
  sound_generator_framesiz = sound_generator_freq / (int)hz;

#if 0
  if( ( sound_buf = (libspectrum_signed_word*) malloc( sizeof( libspectrum_signed_word ) *
			    sound_generator_framesiz * sound_channels ) ) ==
      NULL
      || ( tape_buf =
	   malloc( sizeof( libspectrum_signed_word ) *
		   sound_generator_framesiz ) ) == NULL ) {
    if( sound_buf ) {
      free( sound_buf );
      sound_buf = NULL;
    }
    sound_end();
    return;
  }
#endif

//  sound_framesiz = ( float ) settings_current.sound_freq / hz;
  sound_framesiz = sound_generator_freq / (int)hz;

#ifdef HAVE_SAMPLERATE
  if( settings_current.sound_hifi ) {
    if( ( convert_input_buffer = malloc( sizeof( float ) *
					 sound_generator_framesiz *
					 sound_channels ) ) == NULL
	|| ( convert_output_buffer =
	     malloc( sizeof( float ) * sound_framesiz * sound_channels ) ) ==
	NULL ) {
      if( convert_input_buffer ) {
	free( convert_input_buffer );
	convert_input_buffer = NULL;
      }
      sound_end();
      return;
    }
  }

  src_state = src_new( SRC_SINC_MEDIUM_QUALITY, sound_channels, &error );
  if( error ) {
    ui_error( UI_ERROR_ERROR,
	      "error initialising sample rate converter %s",
	      src_strerror( error ) );
    sound_end();
    return;
  }
#endif /* #ifdef HAVE_SAMPLERATE */

/* if we're resuming, we need to be careful about what
 * gets reset. The minimum we can do is the beeper
 * buffer positions, so that's here.
 */
#if 0
  sound_oldpos[0] = sound_oldpos[1] = -1;
  sound_fillpos[0] = sound_fillpos[1] = 0;
#endif

/* this stuff should only happen on the initial call.
 * (We currently assume the new sample rate will be the
 * same as the previous one, hence no need to recalculate
 * things dependent on that.)
 */
#if 0
  if( first_init ) {
    first_init = 0;

    for( f = 0; f < 2; f++ )
      sound_oldval[f] = sound_oldval_orig[f] = 0;
  }
#endif

#if 0
  if( sound_stereo_beeper ) {
    for( f = 0; f < STEREO_BUF_SIZE; f++ )
      pstereobuf[f] = 0;
    pstereopos = 0;
    pstereobufsiz = ( sound_generator_freq * psgap ) / 22000;
  }

  if( sound_stereo_ay ) {
    int pos =
      ( sound_stereo_ay_narrow ? 3 : 6 ) * sound_generator_freq / 8000;

    for( f = 0; f < STEREO_BUF_SIZE; f++ )
      rstereobuf_l[f] = rstereobuf_r[f] = 0;
    rstereopos = 0;

    /* the actual ACB/ABC bit :-) */
    rchan1pos = -pos;
    if( sound_stereo_ay_abc )
      rchan2pos = 0, rchan3pos = pos;
    else
      rchan2pos = pos, rchan3pos = 0;
  }
#endif

#if 0
  ay_tick_incr = ( int ) ( 65536. *
			   libspectrum_timings_ay_speed( machine_current->
							 machine ) /
			   sound_generator_freq );
#endif
  _this->ay_tick_incr = ( int ) ( 65536. * m_fCurrentCLK_AY8910 / sound_generator_freq );	// [TC]
}


#if 0
void
sound_pause( void )
{
  if( sound_enabled )
    sound_end();
}


void
sound_unpause( void )
{
/* No sound if fastloading in progress */
  if( settings_current.fastload && tape_is_playing() )
    return;

  sound_init( settings_current.sound_device );
}
#endif


static void sound_end( CAY8910 *_this )
{
#if 0
  if( sound_enabled ) {
    if( sound_buf ) {
      free( sound_buf );
      sound_buf = NULL;
      free( tape_buf );
      tape_buf = NULL;
    }
    if( convert_input_buffer ) {
      free( convert_input_buffer );
      convert_input_buffer = NULL;
    }
    if( convert_output_buffer ) {
      free( convert_output_buffer );
      convert_output_buffer = NULL;
    }
#ifdef HAVE_SAMPLERATE
    if( src_state )
      src_state = src_delete( src_state );
#endif /* #ifdef HAVE_SAMPLERATE */
    sound_lowlevel_end();
    sound_enabled = 0;
  }
#endif

#if 0
    if( sound_buf ) {
      free( sound_buf );
      sound_buf = NULL;
    }
#endif
}


#if 0
/* write sample to buffer as pseudo-stereo */
static void
sound_write_buf_pstereo( libspectrum_signed_word * out, int c )
{
  int bl = ( c - pstereobuf[ pstereopos ] ) / 2;
  int br = ( c + pstereobuf[ pstereopos ] ) / 2;

  if( bl < -AMPL_BEEPER )
    bl = -AMPL_BEEPER;
  if( br < -AMPL_BEEPER )
    br = -AMPL_BEEPER;
  if( bl > AMPL_BEEPER )
    bl = AMPL_BEEPER;
  if( br > AMPL_BEEPER )
    br = AMPL_BEEPER;

  *out = bl;
  out[1] = br;

  pstereobuf[ pstereopos ] = c;
  pstereopos++;
  if( pstereopos >= pstereobufsiz )
    pstereopos = 0;
}
#endif



/* not great having this as a macro to inline it, but it's only
 * a fairly short routine, and it saves messing about.
 * (XXX ummm, possibly not so true any more :-))
 */
#define AY_GET_SUBVAL( chan ) \
  ( level * 2 * _this->ay_tone_tick[ chan ] / tone_count )

#define AY_DO_TONE( var, chan ) \
  ( var ) = 0;								\
  is_low = 0;								\
  if( level ) {								\
    if( _this->ay_tone_high[ chan ] )						\
      ( var ) = ( level );						\
    else {								\
      ( var ) = -( level );						\
      is_low = 1;							\
    }									\
  }									\
  									\
  _this->ay_tone_tick[ chan ] += tone_count;					\
  count = 0;								\
  while( _this->ay_tone_tick[ chan ] >= _this->ay_tone_period[ chan ] ) {		\
    count++;								\
    _this->ay_tone_tick[ chan ] -= _this->ay_tone_period[ chan ];			\
    _this->ay_tone_high[ chan ] = !_this->ay_tone_high[ chan ];			\
    									\
    /* has to be here, unfortunately... */				\
    if( count == 1 && level && _this->ay_tone_tick[ chan ] < tone_count ) {	\
      if( is_low )							\
        ( var ) += AY_GET_SUBVAL( chan );				\
      else								\
        ( var ) -= AY_GET_SUBVAL( chan );				\
      }									\
    }									\
  									\
  /* if it's changed more than once during the sample, we can't */	\
  /* represent it faithfully. So, just hope it's a sample.      */	\
  /* (That said, this should also help avoid aliasing noise.)   */	\
  if( count > 1 )							\
    ( var ) = -( level )


#if 0
/* add val, correctly delayed on either left or right buffer,
 * to add the AY stereo positioning. This doesn't actually put
 * anything directly in sound_buf, though.
 */
#define GEN_STEREO( pos, val ) \
  if( ( pos ) < 0 ) {							\
    rstereobuf_l[ rstereopos ] += ( val );				\
    rstereobuf_r[ ( rstereopos - pos ) % STEREO_BUF_SIZE ] += ( val );	\
  } else {								\
    rstereobuf_l[ ( rstereopos + pos ) % STEREO_BUF_SIZE ] += ( val );	\
    rstereobuf_r[ rstereopos ] += ( val );				\
  }
#endif


/* bitmasks for envelope */
#define AY_ENV_CONT	8
#define AY_ENV_ATTACK	4
#define AY_ENV_ALT	2
#define AY_ENV_HOLD	1

#ifdef APPLE2IX
// defined above
#else
#define HZ_COMMON_DENOMINATOR 50
#endif

static void sound_ay_overlay(CAY8910 *_this)
{
  int tone_level[3];
  int mixer, envshape;
  int f, g, level, count;
//  libspectrum_signed_word *ptr;
  struct ay_change_tag *change_ptr = _this->ay_change;
  int changes_left = _this->ay_change_count;
  int reg, r;
  int is_low;
  int chan1, chan2, chan3;
  unsigned int tone_count, noise_count;
  libspectrum_dword sfreq, cpufreq;

///* If no AY chip, don't produce any AY sound (!) */
//  if( !machine_current->capabilities & LIBSPECTRUM_MACHINE_CAPABILITY_AY )
//    return;

/* convert change times to sample offsets, use common denominator of 50 to
   avoid overflowing a dword */
  sfreq = sound_generator_freq / HZ_COMMON_DENOMINATOR;
//  cpufreq = machine_current->timings.processor_speed / HZ_COMMON_DENOMINATOR;
  cpufreq = (libspectrum_dword) (m_fCurrentCLK_AY8910 / HZ_COMMON_DENOMINATOR);	// [TC]
  for( f = 0; f < _this->ay_change_count; f++ )
    _this->ay_change[f].ofs = (uint16_t) (( _this->ay_change[f].tstates * sfreq ) / cpufreq);	// [TC] Added cast

  libspectrum_signed_word* pBuf1 = g_ppSoundBuffers[0];
  libspectrum_signed_word* pBuf2 = g_ppSoundBuffers[1];
  libspectrum_signed_word* pBuf3 = g_ppSoundBuffers[2];

//  for( f = 0, ptr = sound_buf; f < sound_generator_framesiz; f++ ) {
  for( f = 0; f < sound_generator_framesiz; f++ ) {
    /* update ay registers. All this sub-frame change stuff
     * is pretty hairy, but how else would you handle the
     * samples in Robocop? :-) It also clears up some other
     * glitches.
     */
    while( changes_left && f >= change_ptr->ofs ) {
      _this->sound_ay_registers[ reg = change_ptr->reg ] = change_ptr->val;
      change_ptr++;
      changes_left--;

      /* fix things as needed for some register changes */
      switch ( reg ) {
      case 0:
      case 1:
      case 2:
      case 3:
      case 4:
      case 5:
	r = reg >> 1;
	/* a zero-len period is the same as 1 */
	_this->ay_tone_period[r] = ( _this->sound_ay_registers[ reg & ~1 ] |
			      ( _this->sound_ay_registers[ reg | 1 ] & 15 ) << 8 );
	if( !_this->ay_tone_period[r] )
	  _this->ay_tone_period[r]++;

	/* important to get this right, otherwise e.g. Ghouls 'n' Ghosts
	 * has really scratchy, horrible-sounding vibrato.
	 */
	if( _this->ay_tone_tick[r] >= _this->ay_tone_period[r] * 2 )
	  _this->ay_tone_tick[r] %= _this->ay_tone_period[r] * 2;
	break;
      case 6:
	_this->ay_noise_tick = 0;
	_this->ay_noise_period = ( _this->sound_ay_registers[ reg ] & 31 );
	break;
      case 11:
      case 12:
	/* this one *isn't* fixed-point */
	_this->ay_env_period =
	  _this->sound_ay_registers[11] | ( _this->sound_ay_registers[12] << 8 );
	break;
      case 13:
	_this->ay_env_internal_tick = _this->ay_env_tick = _this->ay_env_subcycles = 0;
	_this->env_first = 1;
	_this->env_rev = 0;
	_this->env_counter = ( _this->sound_ay_registers[13] & AY_ENV_ATTACK ) ? 0 : 15;
	break;
      }
    }

    /* the tone level if no enveloping is being used */
    for( g = 0; g < 3; g++ )
      tone_level[g] = ay_tone_levels[ _this->sound_ay_registers[ 8 + g ] & 15 ];

    /* envelope */
    envshape = _this->sound_ay_registers[13];
    level = ay_tone_levels[ _this->env_counter ];

    for( g = 0; g < 3; g++ )
      if( _this->sound_ay_registers[ 8 + g ] & 16 )
	tone_level[g] = level;

    /* envelope output counter gets incr'd every 16 AY cycles.
     * Has to be a while, as this is sub-output-sample res.
     */
    _this->ay_env_subcycles += _this->ay_tick_incr;
    noise_count = 0;
    while( _this->ay_env_subcycles >= ( 16 << 16 ) ) {
      _this->ay_env_subcycles -= ( 16 << 16 );
      noise_count++;
      _this->ay_env_tick++;
      while( _this->ay_env_tick >= _this->ay_env_period ) {
	_this->ay_env_tick -= _this->ay_env_period;

	/* do a 1/16th-of-period incr/decr if needed */
	if( _this->env_first ||
	    ( ( envshape & AY_ENV_CONT ) && !( envshape & AY_ENV_HOLD ) ) ) {
	  if( _this->env_rev )
	    _this->env_counter -= ( envshape & AY_ENV_ATTACK ) ? 1 : -1;
	  else
	    _this->env_counter += ( envshape & AY_ENV_ATTACK ) ? 1 : -1;
	  if( _this->env_counter < 0 )
	    _this->env_counter = 0;
	  if( _this->env_counter > 15 )
	    _this->env_counter = 15;
	}

	_this->ay_env_internal_tick++;
	while( _this->ay_env_internal_tick >= 16 ) {
	  _this->ay_env_internal_tick -= 16;

	  /* end of cycle */
	  if( !( envshape & AY_ENV_CONT ) )
	    _this->env_counter = 0;
	  else {
	    if( envshape & AY_ENV_HOLD ) {
	      if( _this->env_first && ( envshape & AY_ENV_ALT ) )
		_this->env_counter = ( _this->env_counter ? 0 : 15 );
	    } else {
	      /* non-hold */
	      if( envshape & AY_ENV_ALT )
		_this->env_rev = !_this->env_rev;
	      else
		_this->env_counter = ( envshape & AY_ENV_ATTACK ) ? 0 : 15;
	    }
	  }

	  _this->env_first = 0;
	}

	/* don't keep trying if period is zero */
	if( !_this->ay_env_period )
	  break;
      }
    }

    /* generate tone+noise... or neither.
     * (if no tone/noise is selected, the chip just shoves the
     * level out unmodified. This is used by some sample-playing
     * stuff.)
     */
    chan1 = tone_level[0];
    chan2 = tone_level[1];
    chan3 = tone_level[2];
    mixer = _this->sound_ay_registers[7];

    _this->ay_tone_subcycles += _this->ay_tick_incr;
    tone_count = _this->ay_tone_subcycles >> ( 3 + 16 );
    _this->ay_tone_subcycles &= ( 8 << 16 ) - 1;

    if( ( mixer & 1 ) == 0 ) {
      level = chan1;
      AY_DO_TONE( chan1, 0 );
    }
    if( ( mixer & 0x08 ) == 0 && _this->noise_toggle )
      chan1 = 0;

    if( ( mixer & 2 ) == 0 ) {
      level = chan2;
      AY_DO_TONE( chan2, 1 );
    }
    if( ( mixer & 0x10 ) == 0 && _this->noise_toggle )
      chan2 = 0;

    if( ( mixer & 4 ) == 0 ) {
      level = chan3;
      AY_DO_TONE( chan3, 2 );
    }
    if( ( mixer & 0x20 ) == 0 && _this->noise_toggle )
      chan3 = 0;

    /* write the sample(s) */
	*pBuf1++ = chan1;	// [TC]
	*pBuf2++ = chan2;	// [TC]
	*pBuf3++ = chan3;	// [TC]
#if 0
    if( !sound_stereo ) {
      /* mono */
      ( *ptr++ ) += chan1 + chan2 + chan3;
    } else {
      if( !sound_stereo_ay ) {
	/* stereo output, but mono AY sound; still,
	 * incr separately in case of beeper pseudostereo.
	 */
	( *ptr++ ) += chan1 + chan2 + chan3;
	( *ptr++ ) += chan1 + chan2 + chan3;
      } else {
	/* stereo with ACB/ABC AY positioning.
	 * Here we use real stereo positions for the channels.
	 * Just because, y'know, it's cool and stuff. No, really. :-)
	 * This is a little tricky, as it works by delaying sounds
	 * on the left or right channels to model the delay you get
	 * in the real world when sounds originate at different places.
	 */
	GEN_STEREO( rchan1pos, chan1 );
	GEN_STEREO( rchan2pos, chan2 );
	GEN_STEREO( rchan3pos, chan3 );
	( *ptr++ ) += rstereobuf_l[ rstereopos ];
	( *ptr++ ) += rstereobuf_r[ rstereopos ];
	rstereobuf_l[ rstereopos ] = rstereobuf_r[ rstereopos ] = 0;
	rstereopos++;
	if( rstereopos >= STEREO_BUF_SIZE )
	  rstereopos = 0;
      }
    }
#endif

    /* update noise RNG/filter */
    _this->ay_noise_tick += noise_count;
    while( _this->ay_noise_tick >= _this->ay_noise_period ) {
      _this->ay_noise_tick -= _this->ay_noise_period;

      if( ( _this->rng & 1 ) ^ ( ( _this->rng & 2 ) ? 1 : 0 ) )
	_this->noise_toggle = !_this->noise_toggle;

      /* rng is 17-bit shift reg, bit 0 is output.
       * input is bit 0 xor bit 2.
       */
      _this->rng |= ( ( _this->rng & 1 ) ^ ( ( _this->rng & 4 ) ? 1 : 0 ) ) ? 0x20000 : 0;
      _this->rng >>= 1;

      /* don't keep trying if period is zero */
      if( !_this->ay_noise_period )
	break;
    }
  }
}

// AppleWin:TC  Holding down ScrollLock will result in lots of AY changes /ay_change_count/
//              - since sound_ay_overlay() is called to consume them.

/* don't make the change immediately; record it for later,
 * to be made by sound_frame() (via sound_ay_overlay()).
 */
void sound_ay_write( CAY8910 *_this, int reg, int val, libspectrum_dword now )
{
  if( _this->ay_change_count < AY_CHANGE_MAX ) {
    _this->ay_change[ _this->ay_change_count ].tstates = now;
    _this->ay_change[ _this->ay_change_count ].reg = ( reg & 15 );
    _this->ay_change[ _this->ay_change_count ].val = val;
    _this->ay_change_count++;
  }
}


/* no need to call this initially, but should be called
 * on reset otherwise.
 */
void sound_ay_reset( CAY8910 *_this )
{
  int f;

  CAY8910_init(_this);	// AppleWin:TC

/* recalculate timings based on new machines ay clock */
  sound_ay_init(_this);

  _this->ay_change_count = 0;
  for( f = 0; f < 16; f++ )
    sound_ay_write( _this, f, 0, 0 );
  for( f = 0; f < 3; f++ )
    _this->ay_tone_high[f] = 0;
  _this->ay_tone_subcycles = _this->ay_env_subcycles = 0;
}


#if 0
/* write stereo or mono beeper sample, and incr ptr */
#define SOUND_WRITE_BUF_BEEPER( ptr, val ) \
  do {							\
    if( sound_stereo_beeper ) {				\
      sound_write_buf_pstereo( ( ptr ), ( val ) );	\
      ( ptr ) += 2;					\
    } else {						\
      *( ptr )++ = ( val );				\
      if( sound_stereo )				\
        *( ptr )++ = ( val );				\
    }							\
  } while(0)

/* the tape version works by writing to a separate mono buffer,
 * which gets added after being generated.
 */
#define SOUND_WRITE_BUF( is_tape, ptr, val ) \
  if( is_tape )					\
    *( ptr )++ = ( val );			\
  else						\
    SOUND_WRITE_BUF_BEEPER( ptr, val )
#endif

#ifdef HAVE_SAMPLERATE
static void
sound_resample( void )
{
  int error;
  SRC_DATA data;

  data.data_in = convert_input_buffer;
  data.input_frames = sound_generator_framesiz;
  data.data_out = convert_output_buffer;
  data.output_frames = sound_framesiz;
  data.src_ratio =
    ( double ) settings_current.sound_freq / sound_generator_freq;
  data.end_of_input = 0;

  src_short_to_float_array( ( const short * ) sound_buf, convert_input_buffer,
			    sound_generator_framesiz * sound_channels );

  while( data.input_frames ) {
    error = src_process( src_state, &data );
    if( error ) {
      ui_error( UI_ERROR_ERROR, "hifi sound downsample error %s",
		src_strerror( error ) );
      sound_end(_this);
      return;
    }

    src_float_to_short_array( convert_output_buffer, ( short * ) sound_buf,
			      data.output_frames_gen * sound_channels );

    sound_lowlevel_frame( sound_buf,
			  data.output_frames_gen * sound_channels );

    data.data_in += data.input_frames_used * sound_channels;
    data.input_frames -= data.input_frames_used;
  }
}
#endif /* #ifdef HAVE_SAMPLERATE */

void sound_frame( CAY8910 *_this )
{
#if 0
  libspectrum_signed_word *ptr, *tptr;
  int f, bchan;
  int ampl = AMPL_BEEPER;

  if( !sound_enabled )
    return;

/* fill in remaining beeper/tape sound */
  ptr =
    sound_buf + ( sound_stereo ? sound_fillpos[0] * 2 : sound_fillpos[0] );
  for( bchan = 0; bchan < 2; bchan++ ) {
    for( f = sound_fillpos[ bchan ]; f < sound_generator_framesiz; f++ )
      SOUND_WRITE_BUF( bchan, ptr, sound_oldval[ bchan ] );

    ptr = tape_buf + sound_fillpos[1];
    ampl = AMPL_TAPE;
  }

/* overlay tape sound */
  ptr = sound_buf;
  tptr = tape_buf;
  for( f = 0; f < sound_generator_framesiz; f++, tptr++ ) {
    ( *ptr++ ) += *tptr;
    if( sound_stereo )
      ( *ptr++ ) += *tptr;
  }
#endif

/* overlay AY sound */
  sound_ay_overlay(_this);

#ifdef HAVE_SAMPLERATE
/* resample from generated frequency down to output frequency if required */
  if( settings_current.sound_hifi )
    sound_resample();
  else
#endif /* #ifdef HAVE_SAMPLERATE */
#if 0
    sound_lowlevel_frame( sound_buf,
			  sound_generator_framesiz * sound_channels );
#endif

#if 0
  sound_oldpos[0] = sound_oldpos[1] = -1;
  sound_fillpos[0] = sound_fillpos[1] = 0;
#endif

  _this->ay_change_count = 0;
}

#if 0
/* two beepers are supported - the real beeper (call with is_tape==0)
 * and a `fake' beeper which lets you hear when a tape is being played.
 */
void
sound_beeper( int is_tape, int on )
{
  libspectrum_signed_word *ptr;
  int newpos, subpos;
  int val, subval;
  int f;
  int bchan = ( is_tape ? 1 : 0 );
  int ampl = ( is_tape ? AMPL_TAPE : AMPL_BEEPER );
  int vol = ampl * 2;

  if( !sound_enabled )
    return;

  val = ( on ? -ampl : ampl );

  if( val == sound_oldval_orig[ bchan ] )
    return;

/* XXX a lookup table might help here, but would need to regenerate it
 * whenever cycles_per_frame were changed (i.e. when machine type changed).
 */
  newpos =
    ( tstates * sound_generator_framesiz ) /
    machine_current->timings.tstates_per_frame;
  subpos =
    ( ( ( libspectrum_signed_qword ) tstates ) * sound_generator_framesiz *
      vol ) / ( machine_current->timings.tstates_per_frame ) - vol * newpos;

/* if we already wrote here, adjust the level.
 */
  if( newpos == sound_oldpos[ bchan ] ) {
    /* adjust it as if the rest of the sample period were all in
     * the new state. (Often it will be, but if not, we'll fix
     * it later by doing this again.)
     */
    if( on )
      beeper_last_subpos[ bchan ] += vol - subpos;
    else
      beeper_last_subpos[ bchan ] -= vol - subpos;
  } else
    beeper_last_subpos[ bchan ] = ( on ? vol - subpos : subpos );

  subval = ampl - beeper_last_subpos[ bchan ];

  if( newpos >= 0 ) {
    /* fill gap from previous position */
    if( is_tape )
      ptr = tape_buf + sound_fillpos[1];
    else
      ptr =
	sound_buf +
	( sound_stereo ? sound_fillpos[0] * 2 : sound_fillpos[0] );

    for( f = sound_fillpos[ bchan ];
	 f < newpos && f < sound_generator_framesiz;
	 f++ )
      SOUND_WRITE_BUF( bchan, ptr, sound_oldval[ bchan ] );

    if( newpos < sound_generator_framesiz ) {
      /* newpos may be less than sound_fillpos, so... */
      if( is_tape )
	ptr = tape_buf + newpos;
      else
	ptr = sound_buf + ( sound_stereo ? newpos * 2 : newpos );

      /* write subsample value */
      SOUND_WRITE_BUF( bchan, ptr, subval );
    }
  }

  sound_oldpos[ bchan ] = newpos;
  sound_fillpos[ bchan ] = newpos + 1;
  sound_oldval[ bchan ] = sound_oldval_orig[ bchan ] = val;
}
#endif

uint8_t* GetAYRegsPtr(struct CAY8910 *_this)
{
    return &(_this->sound_ay_registers[0]);
}

void SetCLK(double CLK)
{
    m_fCurrentCLK_AY8910 = CLK;
}

//

#define SS_YAML_KEY_AY8910 "AY8910"

#define SS_YAML_KEY_TONE0_TICK "Tone0 Tick"
#define SS_YAML_KEY_TONE1_TICK "Tone1 Tick"
#define SS_YAML_KEY_TONE2_TICK "Tone2 Tick"
#define SS_YAML_KEY_TONE0_HIGH "Tone0 High"
#define SS_YAML_KEY_TONE1_HIGH "Tone1 High"
#define SS_YAML_KEY_TONE2_HIGH "Tone2 High"
#define SS_YAML_KEY_NOISE_TICK "Noise Tick"
#define SS_YAML_KEY_TONE_SUBCYCLES "Tone Subcycles"
#define SS_YAML_KEY_ENV_SUBCYCLES "Env Subcycles"
#define SS_YAML_KEY_ENV_INTERNAL_TICK "Env Internal Tick"
#define SS_YAML_KEY_ENV_TICK "Env Tick"
#define SS_YAML_KEY_TICK_INCR "Tick Incr"
#define SS_YAML_KEY_TONE0_PERIOD "Tone0 Period"
#define SS_YAML_KEY_TONE1_PERIOD "Tone1 Period"
#define SS_YAML_KEY_TONE2_PERIOD "Tone2 Period"
#define SS_YAML_KEY_NOISE_PERIOD "Noise Period"
#define SS_YAML_KEY_ENV_PERIOD "Env Period"
#define SS_YAML_KEY_RNG "RNG"
#define SS_YAML_KEY_NOISE_TOGGLE "Noise Toggle"
#define SS_YAML_KEY_ENV_FIRST "Env First"
#define SS_YAML_KEY_ENV_REV "Env Rev"
#define SS_YAML_KEY_ENV_COUNTER "Env Counter"

#define SS_YAML_KEY_REGISTERS "Registers"
#define SS_YAML_KEY_REG_TONE0_PERIOD "Tone0 Period"
#define SS_YAML_KEY_REG_TONE1_PERIOD "Tone1 Period"
#define SS_YAML_KEY_REG_TONE2_PERIOD "Tone2 Period"
#define SS_YAML_KEY_REG_NOISE_PERIOD "Noise Period"
#define SS_YAML_KEY_REG_MIXER "Mixer"
#define SS_YAML_KEY_REG_VOL0 "Vol0"
#define SS_YAML_KEY_REG_VOL1 "Vol1"
#define SS_YAML_KEY_REG_VOL2 "Vol2"
#define SS_YAML_KEY_REG_ENV_PERIOD "Env Period"
#define SS_YAML_KEY_REG_ENV_SHAPE "Env Shape"
#define SS_YAML_KEY_REG_PORTA "PortA"
#define SS_YAML_KEY_REG_PORTB "PortB"

#define SS_YAML_KEY_CHANGE "Change"
#define SS_YAML_VALUE_CHANGE_FORMAT "%d, %d, 0x%1X, 0x%02X"

#if 1 // APPLE2IX
static bool _saveStateData32(StateHelper_s *helper, uint32_t data) {
    int fd = helper->fd;
    uint8_t serialized[sizeof(uint32_t)] = { 0 };
    serialized[0] = (uint8_t)((data & 0xFF000000) >> 24);
    serialized[1] = (uint8_t)((data & 0xFF0000  ) >> 16);
    serialized[2] = (uint8_t)((data & 0xFF00    ) >>  8);
    serialized[3] = (uint8_t)((data & 0xFF      ) >>  0);
    return helper->save(fd, serialized, sizeof(serialized));
}

static bool _loadStateData32(StateHelper_s *helper, uint32_t *data) {
    int fd = helper->fd;
    uint8_t serialized[sizeof(uint32_t)] = { 0 };
    if (!helper->load(fd, serialized, sizeof(uint32_t))) {
        return false;
    }
    *data  = (uint32_t)(serialized[0] << 24) |
             (uint32_t)(serialized[1] << 16) |
             (uint32_t)(serialized[2] <<  8) |
             (uint32_t)(serialized[3] <<  0);
    return true;
}

static bool _saveState(StateHelper_s *helper, struct CAY8910 *_this) {
    int fd = helper->fd;

    bool saved = false;
    do {
        if (!_saveStateData32(helper, _this->ay_tone_tick[0])) {
            break;
        }
        if (!_saveStateData32(helper, _this->ay_tone_tick[1])) {
            break;
        }
        if (!_saveStateData32(helper, _this->ay_tone_tick[2])) {
            break;
        }
        if (!_saveStateData32(helper, _this->ay_tone_high[0])) {
            break;
        }
        if (!_saveStateData32(helper, _this->ay_tone_high[1])) {
            break;
        }
        if (!_saveStateData32(helper, _this->ay_tone_high[2])) {
            break;
        }
        if (!_saveStateData32(helper, _this->ay_noise_tick)) {
            break;
        }
        if (!_saveStateData32(helper, _this->ay_tone_subcycles)) {
            break;
        }
        if (!_saveStateData32(helper, _this->ay_env_subcycles)) {
            break;
        }
        if (!_saveStateData32(helper, _this->ay_env_internal_tick)) {
            break;
        }
        if (!_saveStateData32(helper, _this->ay_env_tick)) {
            break;
        }
        if (!_saveStateData32(helper, _this->ay_tick_incr)) {
            break;
        }
        if (!_saveStateData32(helper, _this->ay_tone_period[0])) {
            break;
        }
        if (!_saveStateData32(helper, _this->ay_tone_period[1])) {
            break;
        }
        if (!_saveStateData32(helper, _this->ay_tone_period[2])) {
            break;
        }
        if (!_saveStateData32(helper, _this->ay_noise_period)) {
            break;
        }
        if (!_saveStateData32(helper, _this->ay_env_period)) {
            break;
        }
        if (!_saveStateData32(helper, _this->rng)) {
            break;
        }
        if (!_saveStateData32(helper, _this->noise_toggle)) {
            break;
        }
        if (!_saveStateData32(helper, _this->env_first)) {
            break;
        }
        if (!_saveStateData32(helper, _this->env_rev)) {
            break;
        }
        if (!_saveStateData32(helper, _this->env_counter)) {
            break;
        }

        // Registers
        {
            bool success = true;
            for (unsigned int i=0; i<16; i++) {
                uint8_t reg = _this->sound_ay_registers[i];
                if (!helper->save(fd, &reg, sizeof(reg))) {
                    success = false;
                    break;
                }
            }
            if (!success) {
                break;
            }
        }

        // Queued changes
        assert(_this->ay_change_count >= 0);
        if (!_saveStateData32(helper, _this->ay_change_count)) {
            break;
        }
        if (_this->ay_change_count)
        {
            bool success = true;
            for (int i=0; i<_this->ay_change_count; i++) {
                if (!_saveStateData32(helper, (uint32_t)_this->ay_change[i].tstates)) {
                    success = false;
                    break;
                }
                if (!_saveStateData32(helper, (uint32_t)(_this->ay_change[i].ofs))) {
                    success = false;
                    break;
                }
                uint8_t reg = _this->ay_change[i].reg;
                if (!helper->save(fd, &reg, sizeof(reg))) {
                    success = false;
                    break;
                }
                uint8_t val = _this->ay_change[i].val;
                if (!helper->save(fd, &val, sizeof(val))) {
                    success = false;
                    break;
                }
            }
            if (!success) {
                break;
            }
        }

        saved = true;

    } while (0);

    return saved;
}

static bool _loadState(StateHelper_s *helper, struct CAY8910 *_this) {
    int fd = helper->fd;

    bool loaded = false;
    do {
        if (!_loadStateData32(helper, &(_this->ay_tone_tick[0]))) {
            break;
        }
        if (!_loadStateData32(helper, &(_this->ay_tone_tick[1]))) {
            break;
        }
        if (!_loadStateData32(helper, &(_this->ay_tone_tick[2]))) {
            break;
        }
        if (!_loadStateData32(helper, &(_this->ay_tone_high[0]))) {
            break;
        }
        if (!_loadStateData32(helper, &(_this->ay_tone_high[1]))) {
            break;
        }
        if (!_loadStateData32(helper, &(_this->ay_tone_high[2]))) {
            break;
        }
        if (!_loadStateData32(helper, &(_this->ay_noise_tick))) {
            break;
        }
        if (!_loadStateData32(helper, &(_this->ay_tone_subcycles))) {
            break;
        }
        if (!_loadStateData32(helper, &(_this->ay_env_subcycles))) {
            break;
        }
        if (!_loadStateData32(helper, &(_this->ay_env_internal_tick))) {
            break;
        }
        if (!_loadStateData32(helper, &(_this->ay_env_tick))) {
            break;
        }
        if (!_loadStateData32(helper, &(_this->ay_tick_incr))) {
            break;
        }
        if (!_loadStateData32(helper, &(_this->ay_tone_period[0]))) {
            break;
        }
        if (!_loadStateData32(helper, &(_this->ay_tone_period[1]))) {
            break;
        }
        if (!_loadStateData32(helper, &(_this->ay_tone_period[2]))) {
            break;
        }
        if (!_loadStateData32(helper, &(_this->ay_noise_period))) {
            break;
        }
        if (!_loadStateData32(helper, &(_this->ay_env_period))) {
            break;
        }
        if (!_loadStateData32(helper, (uint32_t *)&(_this->rng))) {
            break;
        }
        if (!_loadStateData32(helper, (uint32_t *)&(_this->noise_toggle))) {
            break;
        }
        if (!_loadStateData32(helper, (uint32_t *)&(_this->env_first))) {
            break;
        }
        if (!_loadStateData32(helper, (uint32_t *)&(_this->env_rev))) {
            break;
        }
        if (!_loadStateData32(helper, (uint32_t *)&(_this->env_counter))) {
            break;
        }

        // Registers
        {
            bool success = true;
            for (unsigned int i=0; i<16; i++) {
                if (!helper->load(fd, &(_this->sound_ay_registers[i]), sizeof(_this->sound_ay_registers[i]))) {
                    success = false;
                    break;
                }
            }
            if (!success) {
                break;
            }
        }

        // Queued changes
        if (!_loadStateData32(helper, (uint32_t *)&(_this->ay_change_count))) {
            break;
        }
        assert(_this->ay_change_count >= 0);
        if (_this->ay_change_count)
        {
            bool success = true;
            for (int i=0; i<_this->ay_change_count; i++) {
                if (!_loadStateData32(helper, (uint32_t *)&(_this->ay_change[i].tstates))) {
                    success = false;
                    break;
                }
                uint32_t ofs = 0;
                if (!_loadStateData32(helper, &ofs)) {
                    success = false;
                    break;
                }
                assert(ofs <= 65535);
                _this->ay_change[i].ofs = (uint16_t)ofs;

                if (!helper->load(fd, (uint8_t *)&(_this->ay_change[i].reg), sizeof(_this->ay_change[i].reg))) {
                    success = false;
                    break;
                }
                if (!helper->load(fd, (uint8_t *)&(_this->ay_change[i].val), sizeof(_this->ay_change[i].val))) {
                    success = false;
                    break;
                }
            }

            if (!success) {
                break;
            }
        }

        loaded = true;
    } while (0);

    return loaded;
}

#   if TESTING
static int _assert_testData32(const uint32_t data32, uint8_t **exData) {
    uint8_t *expected = *exData;
    uint32_t d32 = (uint32_t)(expected[0] << 24) |
                   (uint32_t)(expected[1] << 16) |
                   (uint32_t)(expected[2] <<  8) |
                   (uint32_t)(expected[3] <<  0);
    ASSERT(d32 == data32);
    *exData += 4;
    PASS();
}

int _testStateA2V2(struct CAY8910 *_this, uint8_t **exData) {

    uint8_t *p8 = *exData;

    _assert_testData32(_this->ay_tone_tick[0], &p8);
    _assert_testData32(_this->ay_tone_tick[1], &p8);
    _assert_testData32(_this->ay_tone_tick[2], &p8);
    _assert_testData32(_this->ay_tone_high[0], &p8);
    _assert_testData32(_this->ay_tone_high[1], &p8);
    _assert_testData32(_this->ay_tone_high[2], &p8);

    _assert_testData32(_this->ay_noise_tick, &p8);
    _assert_testData32(_this->ay_tone_subcycles, &p8);
    _assert_testData32(_this->ay_env_subcycles, &p8);
    _assert_testData32(_this->ay_env_internal_tick, &p8);
    _assert_testData32(_this->ay_env_tick, &p8);

    //_assert_testData32(_this->ay_tick_incr, &p8); -- IGNORE... this will change depending on sample rate
    p8 += 4;

    _assert_testData32(_this->ay_tone_period[0], &p8);
    _assert_testData32(_this->ay_tone_period[1], &p8);
    _assert_testData32(_this->ay_tone_period[2], &p8);

    _assert_testData32(_this->ay_noise_period, &p8);
    _assert_testData32(_this->ay_env_period, &p8);
    _assert_testData32(_this->rng, &p8);
    _assert_testData32(_this->noise_toggle, &p8);
    _assert_testData32(_this->env_first, &p8);
    _assert_testData32(_this->env_rev, &p8);
    _assert_testData32(_this->env_counter, &p8);

    // Registers
    for (unsigned int i=0; i<16; i++) {
        ASSERT(_this->sound_ay_registers[i] == *p8++);
    }

    int changeCount = _this->ay_change_count;
    _assert_testData32(changeCount, &p8);
    ASSERT(changeCount >= 0);

    if (changeCount) {
        for (int i=0; i<changeCount; i++) {
            _assert_testData32(_this->ay_change[i].tstates, &p8);
            _assert_testData32(_this->ay_change[i].ofs, &p8);
            ASSERT(_this->ay_change[i].reg == *p8++);
            ASSERT(_this->ay_change[i].val == *p8++);
        }
    }

    *exData = p8;

    PASS();
}
#   endif


#else
void CAY8910::SaveSnapshot(YamlSaveHelper& yamlSaveHelper, std::string& suffix)
{
	std::string unit = std::string(SS_YAML_KEY_AY8910) + suffix;
	YamlSaveHelper::Label label(yamlSaveHelper, "%s:\n", unit.c_str());

	yamlSaveHelper.SaveUint(SS_YAML_KEY_TONE0_TICK, ay_tone_tick[0]);
	yamlSaveHelper.SaveUint(SS_YAML_KEY_TONE1_TICK, ay_tone_tick[1]);
	yamlSaveHelper.SaveUint(SS_YAML_KEY_TONE2_TICK, ay_tone_tick[2]);
	yamlSaveHelper.SaveUint(SS_YAML_KEY_TONE0_HIGH, ay_tone_high[0]);
	yamlSaveHelper.SaveUint(SS_YAML_KEY_TONE1_HIGH, ay_tone_high[1]);
	yamlSaveHelper.SaveUint(SS_YAML_KEY_TONE2_HIGH, ay_tone_high[2]);
	yamlSaveHelper.SaveUint(SS_YAML_KEY_NOISE_TICK, ay_noise_tick);
	yamlSaveHelper.SaveUint(SS_YAML_KEY_TONE_SUBCYCLES, ay_tone_subcycles);
	yamlSaveHelper.SaveUint(SS_YAML_KEY_ENV_SUBCYCLES, ay_env_subcycles);
	yamlSaveHelper.SaveUint(SS_YAML_KEY_ENV_INTERNAL_TICK, ay_env_internal_tick);
	yamlSaveHelper.SaveUint(SS_YAML_KEY_ENV_TICK, ay_env_tick);
	yamlSaveHelper.SaveUint(SS_YAML_KEY_TICK_INCR, ay_tick_incr);
	yamlSaveHelper.SaveUint(SS_YAML_KEY_TONE0_PERIOD, ay_tone_period[0]);
	yamlSaveHelper.SaveUint(SS_YAML_KEY_TONE1_PERIOD, ay_tone_period[1]);
	yamlSaveHelper.SaveUint(SS_YAML_KEY_TONE2_PERIOD, ay_tone_period[2]);
	yamlSaveHelper.SaveUint(SS_YAML_KEY_NOISE_PERIOD, ay_noise_period);
        // APPLE2IX FIXME WHAT ABOUT : SS_YAML_KEY_ENV_PERIOD "Env Period" ... AppleWin forgot about this? Also do this on load ...
	yamlSaveHelper.SaveUint(SS_YAML_KEY_RNG, rng);
	yamlSaveHelper.SaveUint(SS_YAML_KEY_NOISE_TOGGLE, noise_toggle);
	yamlSaveHelper.SaveUint(SS_YAML_KEY_ENV_FIRST, env_first);
	yamlSaveHelper.SaveUint(SS_YAML_KEY_ENV_REV, env_rev);
	yamlSaveHelper.SaveUint(SS_YAML_KEY_ENV_COUNTER, env_counter);

	// New label
	{
		YamlSaveHelper::Label registers(yamlSaveHelper, "%s:\n", SS_YAML_KEY_REGISTERS);

		yamlSaveHelper.SaveHexUint12(SS_YAML_KEY_REG_TONE0_PERIOD, (UINT)(sound_ay_registers[1]<<8) | sound_ay_registers[0]);
		yamlSaveHelper.SaveHexUint12(SS_YAML_KEY_REG_TONE1_PERIOD, (UINT)(sound_ay_registers[3]<<8) | sound_ay_registers[2]);
		yamlSaveHelper.SaveHexUint12(SS_YAML_KEY_REG_TONE2_PERIOD, (UINT)(sound_ay_registers[5]<<8) | sound_ay_registers[4]);
		yamlSaveHelper.SaveHexUint8(SS_YAML_KEY_REG_NOISE_PERIOD, sound_ay_registers[6]);
		yamlSaveHelper.SaveHexUint8(SS_YAML_KEY_REG_MIXER, sound_ay_registers[7]);
		yamlSaveHelper.SaveHexUint8(SS_YAML_KEY_REG_VOL0, sound_ay_registers[8]);
		yamlSaveHelper.SaveHexUint8(SS_YAML_KEY_REG_VOL1, sound_ay_registers[9]);
		yamlSaveHelper.SaveHexUint8(SS_YAML_KEY_REG_VOL2, sound_ay_registers[10]);
		yamlSaveHelper.SaveHexUint16(SS_YAML_KEY_REG_ENV_PERIOD, (UINT)(sound_ay_registers[12]<<8) | sound_ay_registers[11]);
		yamlSaveHelper.SaveHexUint4(SS_YAML_KEY_REG_ENV_SHAPE, sound_ay_registers[13]);
		yamlSaveHelper.SaveHexUint8(SS_YAML_KEY_REG_PORTA, sound_ay_registers[14]);
		yamlSaveHelper.SaveHexUint8(SS_YAML_KEY_REG_PORTB, sound_ay_registers[15]);
	}

	// New label
	if (ay_change_count)
	{
		YamlSaveHelper::Label change(yamlSaveHelper, "%s:\n", SS_YAML_KEY_CHANGE);

		for (int i=0; i<ay_change_count; i++)
			yamlSaveHelper.Save("0x%04X: " SS_YAML_VALUE_CHANGE_FORMAT "\n", i, ay_change[i].tstates, ay_change[i].ofs, ay_change[i].reg, ay_change[i].val);
	}
}

bool CAY8910::LoadSnapshot(YamlLoadHelper& yamlLoadHelper, std::string& suffix)
{
	std::string unit = std::string(SS_YAML_KEY_AY8910) + suffix;
	if (!yamlLoadHelper.GetSubMap(unit))
		throw std::string("Card: Expected key: ") + unit;

	ay_tone_tick[0] = yamlLoadHelper.LoadUint(SS_YAML_KEY_TONE0_TICK);
	ay_tone_tick[1] = yamlLoadHelper.LoadUint(SS_YAML_KEY_TONE1_TICK);
	ay_tone_tick[2] = yamlLoadHelper.LoadUint(SS_YAML_KEY_TONE2_TICK);
	ay_tone_high[0] = yamlLoadHelper.LoadUint(SS_YAML_KEY_TONE0_HIGH);
	ay_tone_high[1] = yamlLoadHelper.LoadUint(SS_YAML_KEY_TONE1_HIGH);
	ay_tone_high[2] = yamlLoadHelper.LoadUint(SS_YAML_KEY_TONE2_HIGH);
	ay_noise_tick = yamlLoadHelper.LoadUint(SS_YAML_KEY_NOISE_TICK);
	ay_tone_subcycles = yamlLoadHelper.LoadUint(SS_YAML_KEY_TONE_SUBCYCLES);
	ay_env_subcycles = yamlLoadHelper.LoadUint(SS_YAML_KEY_ENV_SUBCYCLES);
	ay_env_internal_tick = yamlLoadHelper.LoadUint(SS_YAML_KEY_ENV_INTERNAL_TICK);
	ay_env_tick = yamlLoadHelper.LoadUint(SS_YAML_KEY_ENV_TICK);
	ay_tick_incr = yamlLoadHelper.LoadUint(SS_YAML_KEY_TICK_INCR);
	ay_tone_period[0] = yamlLoadHelper.LoadUint(SS_YAML_KEY_TONE0_PERIOD);
	ay_tone_period[1] = yamlLoadHelper.LoadUint(SS_YAML_KEY_TONE1_PERIOD);
	ay_tone_period[2] = yamlLoadHelper.LoadUint(SS_YAML_KEY_TONE2_PERIOD);
	ay_noise_period = yamlLoadHelper.LoadUint(SS_YAML_KEY_NOISE_PERIOD);
	rng = yamlLoadHelper.LoadUint(SS_YAML_KEY_RNG);
	noise_toggle = yamlLoadHelper.LoadUint(SS_YAML_KEY_NOISE_TOGGLE);
	env_first = yamlLoadHelper.LoadUint(SS_YAML_KEY_ENV_FIRST);
	env_rev = yamlLoadHelper.LoadUint(SS_YAML_KEY_ENV_REV);
	env_counter = yamlLoadHelper.LoadUint(SS_YAML_KEY_ENV_COUNTER);

	if (!yamlLoadHelper.GetSubMap(SS_YAML_KEY_REGISTERS))
		throw std::string("Card: Expected key: ") + SS_YAML_KEY_REGISTERS;

	USHORT period = (USHORT) yamlLoadHelper.LoadUint(SS_YAML_KEY_REG_TONE0_PERIOD);
	sound_ay_registers[0] = period & 0xff;
	sound_ay_registers[1] = (period >> 8) & 0xf;
	period = (USHORT) yamlLoadHelper.LoadUint(SS_YAML_KEY_REG_TONE1_PERIOD);
	sound_ay_registers[2] = period & 0xff;
	sound_ay_registers[3] = (period >> 8) & 0xf;
	period = (USHORT) yamlLoadHelper.LoadUint(SS_YAML_KEY_REG_TONE2_PERIOD);
	sound_ay_registers[4] = period & 0xff;
	sound_ay_registers[5] = (period >> 8) & 0xf;
	sound_ay_registers[6] = yamlLoadHelper.LoadUint(SS_YAML_KEY_REG_NOISE_PERIOD);
	sound_ay_registers[7] = yamlLoadHelper.LoadUint(SS_YAML_KEY_REG_MIXER);
	sound_ay_registers[8] = yamlLoadHelper.LoadUint(SS_YAML_KEY_REG_VOL0);
	sound_ay_registers[9] = yamlLoadHelper.LoadUint(SS_YAML_KEY_REG_VOL1);
	sound_ay_registers[10] = yamlLoadHelper.LoadUint(SS_YAML_KEY_REG_VOL2);
	period = (USHORT) yamlLoadHelper.LoadUint(SS_YAML_KEY_REG_ENV_PERIOD);
	sound_ay_registers[11] = period & 0xff;
	sound_ay_registers[12] = period >> 8;
	sound_ay_registers[13] = yamlLoadHelper.LoadUint(SS_YAML_KEY_REG_ENV_SHAPE);
	sound_ay_registers[14] = yamlLoadHelper.LoadUint(SS_YAML_KEY_REG_PORTA);
	sound_ay_registers[15] = yamlLoadHelper.LoadUint(SS_YAML_KEY_REG_PORTB);

	yamlLoadHelper.PopMap();

	ay_change_count = 0;
	if (yamlLoadHelper.GetSubMap(SS_YAML_KEY_CHANGE))
	{
		while(1)
		{
			char szIndex[7];
			sprintf_s(szIndex, sizeof(szIndex), "0x%04X", ay_change_count);

			bool bFound;
			std::string value = yamlLoadHelper.LoadString_NoThrow(szIndex, bFound);
			if (!bFound)
				break;	// done

			if(4 != sscanf_s(value.c_str(), SS_YAML_VALUE_CHANGE_FORMAT,
				&ay_change[ay_change_count].tstates,
				&ay_change[ay_change_count].ofs,
				&ay_change[ay_change_count].reg,
				&ay_change[ay_change_count].val))
				throw std::string("Card: AY8910: Failed to scanf change list");

			ay_change_count++;
			if (ay_change_count > AY_CHANGE_MAX)
				throw std::string("Card: AY8910: Too many changes");
		}

		yamlLoadHelper.PopMap();
	}

	yamlLoadHelper.PopMap();

	return true;
}
#endif

///////////////////////////////////////////////////////////////////////////////

// AY8910 interface

#ifndef APPLE2IX
#include "CPU.h"	// For g_nCumulativeCycles
#endif

static CAY8910 g_AY8910[MAX_8910];
#ifdef APPLE2IX
static uint64_t g_uLastCumulativeCycles = 0;
#else
static unsigned __int64 g_uLastCumulativeCycles = 0;
#endif

#if MB_TRACING
void _mb_trace_AY8910(int chip, FILE *mb_trace_fp) {
    if (!mb_trace_fp) {
        return;
    }
    assert(chip < MAX_8910);
    CAY8910 *_this = &g_AY8910[chip];

    fprintf(mb_trace_fp, "\tAY8910(%d):\n", chip);

    fprintf(mb_trace_fp, "\t\tay_tone_tick:%u,%u,%u\n", _this->ay_tone_tick[0], _this->ay_tone_tick[1], _this->ay_tone_tick[2]);
    fprintf(mb_trace_fp, "\t\tay_tone_high:%u,%u,%u\n", _this->ay_tone_high[0], _this->ay_tone_high[1], _this->ay_tone_high[2]);

    fprintf(mb_trace_fp, "\t\tay_noise_tick:%u ay_tone_subcycles:%u ay_env_subcycles:%u\n", _this->ay_noise_tick, _this->ay_tone_subcycles, _this->ay_env_subcycles);
    fprintf(mb_trace_fp, "\t\tay_env_internal_tick:%u ay_env_tick:%u ay_tick_incr:%u\n", _this->ay_env_internal_tick, _this->ay_env_tick, _this->ay_tick_incr);

    fprintf(mb_trace_fp, "\t\tay_tone_period:%u,%u,%u\n", _this->ay_tone_period[0], _this->ay_tone_period[1], _this->ay_tone_period[2]);

    fprintf(mb_trace_fp, "\t\tay_noise_period:%u ay_env_period:%u rng:%d noise_toggle:%d\n", _this->ay_noise_period, _this->ay_env_period, _this->rng, _this->noise_toggle);
    fprintf(mb_trace_fp, "\t\tenv_first:%d env_rev:%d env_counter:%d\n", _this->env_first, _this->env_rev, _this->env_counter);

    fprintf(mb_trace_fp, "\t\tenv_first:%d env_rev:%d env_counter:%d\n", _this->env_first, _this->env_rev, _this->env_counter);
    fprintf(mb_trace_fp, "\t\tsound_ay_registers: %02X,%02X,%02X,%02X,%02X,%02X,%02X,%02X,%02X,%02X,%02X,%02X,%02X,%02X,%02X,%02X\n", _this->sound_ay_registers[0], _this->sound_ay_registers[1], _this->sound_ay_registers[2], _this->sound_ay_registers[3], _this->sound_ay_registers[4], _this->sound_ay_registers[5], _this->sound_ay_registers[6], _this->sound_ay_registers[7], _this->sound_ay_registers[8], _this->sound_ay_registers[9], _this->sound_ay_registers[10], _this->sound_ay_registers[11], _this->sound_ay_registers[12], _this->sound_ay_registers[13], _this->sound_ay_registers[14], _this->sound_ay_registers[15]);

    fprintf(mb_trace_fp, "\t\tay_change_count:(%d)\n", _this->ay_change_count);
    for (unsigned int i=0; i<_this->ay_change_count; i++) {
        fprintf(mb_trace_fp, "\t\t\t%u: tstates:%lu ofs:%04X reg:%02X val:%02X\n", i, _this->ay_change[i].tstates, _this->ay_change[i].ofs, _this->ay_change[i].reg, _this->ay_change[i].val);
    }
}
#endif

void _AYWriteReg(int chip, int r, int v
#if MB_TRACING
        , FILE *mb_trace_fp
#endif
        )
{
	libspectrum_dword uOffset = (libspectrum_dword) (cycles_count_total - g_uLastCumulativeCycles);
#if MB_TRACING
        if (mb_trace_fp) {
            fprintf(mb_trace_fp, "\t uOffset: %lu\n", uOffset);
        }
#endif
	sound_ay_write(&g_AY8910[chip], r, v, uOffset);
}

void AY8910_reset(int chip)
{
	// Don't reset the AY CLK, as this is a property of the card (MB/Phasor), not the AY chip
	sound_ay_reset(&g_AY8910[chip]);	// Calls: sound_ay_init();
}

void AY8910UpdateSetCycles()
{
	g_uLastCumulativeCycles = cycles_count_total;
}

void AY8910Update(int chip, int16_t** buffer, int nNumSamples)
{
	AY8910UpdateSetCycles();

	sound_generator_framesiz = nNumSamples;
	g_ppSoundBuffers = buffer;
	sound_frame(&g_AY8910[chip]);
}

void AY8910_InitAll(int nClock, unsigned long nSampleRate)
{
	for (unsigned int i=0; i<MAX_8910; i++)
	{
		sound_init(&g_AY8910[i], NULL, nSampleRate);	// Inits mainly static members (except ay_tick_incr)
		sound_ay_init(&g_AY8910[i]);
	}
}

void AY8910_InitClock(int nClock, unsigned long nSampleRate)
{
	SetCLK( (double)nClock );
	for (unsigned int i=0; i<MAX_8910; i++)
	{
		sound_init(&g_AY8910[i], NULL, nSampleRate);	// ay_tick_incr is dependent on AY_CLK
	}
}

uint8_t* AY8910_GetRegsPtr(unsigned int uChip)
{
	if(uChip >= MAX_8910)
		return NULL;

	return GetAYRegsPtr(&g_AY8910[uChip]);
}

#if 1 // APPLE2IX
bool _ay8910_saveState(StateHelper_s *helper, unsigned int chip) {
    assert(chip < MAX_8910);
    return _saveState(helper, &g_AY8910[chip]);
}

bool _ay8910_loadState(StateHelper_s *helper, unsigned int chip) {
    assert(chip < MAX_8910);
    return _loadState(helper, &g_AY8910[chip]);
}

#   if TESTING
int _ay8910_testAssertA2V2(unsigned int chip, uint8_t **exData) {
    return _testStateA2V2(&g_AY8910[chip], exData);
}
#   endif

#else
UINT AY8910_SaveSnapshot(YamlSaveHelper& yamlSaveHelper, UINT uChip, std::string& suffix)
{
	if (uChip >= MAX_8910)
		return 0;

	g_AY8910[uChip].SaveSnapshot(yamlSaveHelper, suffix);
	return 1;
}

UINT AY8910_LoadSnapshot(YamlLoadHelper& yamlLoadHelper, UINT uChip, std::string& suffix)
{
	if (uChip >= MAX_8910)
		return 0;

	return g_AY8910[uChip].LoadSnapshot(yamlLoadHelper, suffix) ? 1 : 0;
}
#endif

