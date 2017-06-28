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

#ifndef AY8910_H
#define AY8910_H

#define MAX_8910 4

//-------------------------------------
// MAME interface

void _AYWriteReg(int chip, int r, int v
#if MB_TRACING
        , FILE *mb_trace_fp
#endif
        );
//void AY8910_write_ym(int chip, int addr, int data);
void AY8910_reset(int chip);
void AY8910Update(int chip, int16_t** buffer, int nNumSamples);

void AY8910_InitAll(int nClock, unsigned long nSampleRate);
void AY8910_InitClock(int nClock, unsigned long nSampleRate);
uint8_t* AY8910_GetRegsPtr(unsigned int uChip);

void AY8910UpdateSetCycles();

#if 1 // APPLE2IX
bool _ay8910_saveState(StateHelper_s *helper, unsigned int chip);
bool _ay8910_loadState(StateHelper_s *helper, unsigned int chip);
#   if TESTING
int _ay8910_testAssertA2V2(unsigned int chip, uint8_t **exData);
#   endif
#else
UINT AY8910_SaveSnapshot(class YamlSaveHelper& yamlSaveHelper, UINT uChip, std::string& suffix);
UINT AY8910_LoadSnapshot(class YamlLoadHelper& yamlLoadHelper, UINT uChip, std::string& suffix);
#endif
#if MB_TRACING
void _mb_trace_AY8910(int chip, FILE *mb_trace_fp);
#endif

//-------------------------------------
// FUSE stuff

typedef unsigned long libspectrum_dword;
typedef uint8_t libspectrum_byte;
typedef int16_t libspectrum_signed_word;

struct CAY8910;

/* max. number of sub-frame AY port writes allowed;
 * given the number of port writes theoretically possible in a
 * 50th I think this should be plenty.
 */
#define AY_CHANGE_MAX		8000

/*
class CAY8910
{
public:
*/
	void CAY8910_init(struct CAY8910 *_this);

	void sound_ay_init(struct CAY8910 *_this);
	//void sound_init(struct CAY8910 *_this, const char *device);
	void sound_ay_write(struct CAY8910 *_this, int reg, int val, libspectrum_dword now);
	void sound_ay_reset(struct CAY8910 *_this);
	void sound_frame(struct CAY8910 *_this);
	uint8_t* GetAYRegsPtr(struct CAY8910 *_this);
	void SetCLK(double CLK);
#if 1 // APPLE2IX
#else
	void SaveSnapshot(class YamlSaveHelper& yamlSaveHelper, std::string& suffix);
	bool LoadSnapshot(class YamlLoadHelper& yamlLoadHelper, std::string& suffix);
#endif

/*
private:
	void init( void );
	void sound_end( void );
	void sound_ay_overlay( void );
*/

typedef struct ay_change_tag
{
    libspectrum_dword tstates;
    unsigned short ofs;
    unsigned char reg, val;
} ay_change_tag;

/*
private:
*/
typedef struct CAY8910
{
	/* foo_subcycles are fixed-point with low 16 bits as fractional part.
	 * The other bits count as the chip does.
	 */
	unsigned int ay_tone_tick[3], ay_tone_high[3], ay_noise_tick;
	unsigned int ay_tone_subcycles, ay_env_subcycles;
	unsigned int ay_env_internal_tick, ay_env_tick;
	unsigned int ay_tick_incr;
	unsigned int ay_tone_period[3], ay_noise_period, ay_env_period;

	//static int beeper_last_subpos[2] = { 0, 0 };

	/* Local copy of the AY registers */
	libspectrum_byte sound_ay_registers[16];

	struct ay_change_tag ay_change[ AY_CHANGE_MAX ];
	int ay_change_count;

	// statics from sound_ay_overlay()
	int rng;
	int noise_toggle;
	int env_first, env_rev, env_counter;
} CAY8910;

	// Vars shared between all AY's
	extern double m_fCurrentCLK_AY8910;

#endif
