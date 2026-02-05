// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// $Log:$
//
// DESCRIPTION:
//	System interface for sound.
//
//-----------------------------------------------------------------------------

static const char __attribute__((unused))
rcsid[] = "$Id: i_unix.c,v 1.5 1997/02/03 22:45:10 b1 Exp $";

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include <math.h>

#include <sys/time.h>
#include <sys/types.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "z_zone.h"

#include "i_system.h"
#include "i_sound.h"
#include "m_argv.h"
#include "m_misc.h"
#include "w_wad.h"

#include "doomdef.h"
#include "i_device.h"

#include "musdriver.h"


// The number of internal mixing channels,
//  the samples calculated for each mixing step,
//  the size of the 16bit, 2 hardware channel (stereo)
//  mixing buffer, and the samplerate of the raw data.

// Number of mixer channels.
#define NUM_CHANNELS		8
// Power of two >= the number of mixer channels (bitmasks)
#define NUM_CHANNELS_POW2	8
// Number of output channels. 2 for Stereo (OPL3 requires 2)
#define MIX_CHANNELS		2

// Prefer 44100 because it's exactly 4x the sfx recordings
#define MIX_SAMPLERATE		44100

// Based on the 140 Hz music tick rate
// 22050 / 140 = 157.5 (ideal chunk size) [192]
// 24000 / 140 = 171.4                    [256]
// 44100 / 140 = 315.0                    [384]
#define MIX_CHUNK_SIZE		512

// Sfx step shift (rate divider) 0=11025 1=22050 2=44100
// Must match the MIX_CHUNK_SIZE above
#define SFX_STEP_SHIFT		2

// SB Pro used a fixed 12dB/oct LPF @ 3.2kHz (2-pole Butterworth biquad)
// Tweaking this a little.. things sounded better in the past
#define PCM_CUTOFF_HZ           4400
// Butterworth ~0.707, add a bit of passband droop to emphasize the low end
#define PCM_Q_FACTOR            0.6f

// Low-pass filters
#define OPL_CUTOFF_HZ           (MIX_SAMPLERATE/2)


// --------------------------------------------------------------------------
// MIXER THREAD BEGINS

// All code must LOCK `mix_mutex` to access the data in this zone.
// XXX move towards queued commands into the mixer.

static mutex_t mix_mutex = {0};

static void* sfx_data[NUMSFX] = {0};
static int sfx_length[NUMSFX] = {0};

// Basically, samples from all active internal channels
//  are modifed and added, and stored in the buffer
//  that is submitted to the audio device.

static size_t           mix_max_frames = 0;

// The channel step amount...
static unsigned int	channelstep[NUM_CHANNELS];
// ... and a 0.16 bit remainder of last step.
static unsigned int	channelstepremainder[NUM_CHANNELS];

// The channel data pointers, start and end.
static unsigned char*	channels[NUM_CHANNELS];
static unsigned char*	channelsend[NUM_CHANNELS];

// Time/gametic that the channel started playing,
//  used to determine oldest, which automatically
//  has lowest priority.
// In case number of active sounds exceeds
//  available channels.
static int		channelstart[NUM_CHANNELS];

// The channel handle, assigned when the sound starts.
//  used to stop/modify the sound.
static unsigned int 	channelhandles[NUM_CHANNELS];

// SFX id of the playing sound effect.
// Used to catch duplicates (like chainsaw).
static int		channelids[NUM_CHANNELS];			

// Pitch to stepping lookup, unused.
static int		steptable[256];

// Volume lookups.
static int		vol_lookup[128*256];

// Hardware left and right channel volume lookup.
static int*		channelleftvol_lookup[NUM_CHANNELS];
static int*		channelrightvol_lookup[NUM_CHANNELS];

static unsigned int     nexthandle = 0;


// OPL3 generates a stereo pair for each sample.
static int16_t*         music_downmix = 0;         // downmix buffer at MIX_SAMPLERATE

// Derived 0-127 volume used in mixing.
static Atomic_Int 	music_volume = {127};

// Game has started the music playing.
static Atomic_Int       music_loop = {0};
static Atomic_Ptr       music_songptr = {0};
static Atomic_Ptr 	music_finished = {0};

// Game has paused music (network stall?)
static Atomic_Int 	music_paused = {0};


// PCM filter:
// 2-Pole (2nd‑order) Butterworth biquad LPF at 3.2 kHz (Q = ~0.707)
// Exact −3 dB at 3.2 kHz; −12 dB/oct slope.
// Direct Form II Transposed (DF2T) with z1, z2 states.

typedef struct {
    float b0, b1, b2; // feedforward
    float a1, a2;     // feedback (a0 normalized to 1)
    float z1, z2;     // state
} BiquadLP;

BiquadLP pcm_lpf_left = {0};
BiquadLP pcm_lpf_right = {0};

// Bilinear-transform LPF
static inline void biquadlp_init(BiquadLP *b, float sample_rate, float cutoff_hz, float Q) {
    float w0   = 2.0f * (float)M_PI * cutoff_hz / sample_rate;
    float cw   = cosf(w0);
    float sw   = sinf(w0);
    float alpha= sw / (2.0f * Q);

    float a0 = 1.0f + alpha;
    float a1 = -2.0f * cw;
    float a2 = 1.0f - alpha;
    float b0 = (1.0f - cw) * 0.5f;
    float b1 = 1.0f - cw;
    float b2 = (1.0f - cw) * 0.5f;

    // normalize
    b->b0 = b0 / a0;
    b->b1 = b1 / a0;
    b->b2 = b2 / a0;
    b->a1 = a1 / a0;
    b->a2 = a2 / a0;

    b->z1 = b->z2 = 0.0f;
}

static inline int biquadlp_step(BiquadLP *b, float x) {
    float y = b->b0 * x + b->z1;
    b->z1 = b->b1 * x - b->a1 * y + b->z2;
    b->z2 = b->b2 * x - b->a2 * y;
    return (int)y;              // quantize the sample
}

static void* mixer_last_song = 0;     // last song ptr we received
static mus_driver_t music_driver = {0};

// On the music thread, no LOCK held.
static void mix_music( int mix_frames_needed ) {
	int musvol = Atomic_Get_Int(&music_volume);
	void* song = Atomic_Get_Ptr_Acquire(&music_songptr);
	int loop = Atomic_Get_Int(&music_loop); // after acquire
	if (song != mixer_last_song) {
		// game has stopped the music, or started a new track.
		if (music_driver.playing) {
			musdriver_stop(&music_driver);
		}
		if (song) {
			musdriver_start(&music_driver, song, loop);
		}
		mixer_last_song = song;
	}

	int need_mix = musvol && music_driver.playing && !Atomic_Get_Int(&music_paused);
	if (!need_mix) {
		memset(music_downmix, 0, mix_frames_needed*sizeof(int16_t)*MIX_CHANNELS);
		return;
	}

	// generate OPL samples, tick the music player
	float volume = (float)(musvol) * 2.0f / 127.0f;
	if (!musdriver_generate(&music_driver, music_downmix, mix_frames_needed, volume)) {
		return; // buffer overflow
	}
	if (!music_driver.playing) {
		// finished playing
		Atomic_Set_Ptr(&music_finished, mixer_last_song);
	}
}



//
// This function loops all active (internal) sound
//  channels, retrieves a given number of samples
//  from the raw sound data, modifies it according
//  to the current (internal) channel parameters,
//  mixes the per channel samples into the global
//  mixbuffer, clamping it to the allowed range,
//  and sets up everything for transferring the
//  contents of the mixbuffer to the (two)
//  hardware channels (left and right, that is).
//
// This function currently supports only 16bit.
//
// On the mixer thread. Acquires LOCK.
//
static void mix_samples( int16_t* mixbuffer, int samples_needed )
{
    // Mix current sound data.
    // Data, from raw sound, for right and left.
    register unsigned int	sample;
    register int		dl;
    register int		dr;
  
    // Pointers in mixbuffer, left, right, end.
    int16_t*			leftout;
    int16_t*			rightout;
    int16_t*			leftend;
    int16_t*                    musicbuf;

    // Mixing channel index.
    int				chan;

    // Left and right channel
    //  are in mixbuffer, alternating.
    leftout = mixbuffer;
    rightout = mixbuffer+1;

    // Determine end, for left channel only
    //  (right channel is implicit).
    leftend = mixbuffer + samples_needed;

    // Also mix in the music buffer.
    musicbuf = music_downmix;

    // Need to hold this to access `channels`, `channelstep` etc
    Mutex_Lock(&mix_mutex);

    // Mix sounds into the mixing buffer.
    // Loop over step*samplecount,
    //  that is 512 values for two channels.
    while (leftout != leftend)
    {
	// Reset left/right value. 
	dl = 0;
	dr = 0;

	// Love thy L2 chache - made this a loop.
	// Now more channels could be set at compile time
	//  as well. Thus loop those  channels.
	for ( chan = 0; chan < NUM_CHANNELS; chan++ )
	{
	    // Check channel, if active.
	    if (channels[ chan ])
	    {
		// Get the raw data from the channel. 
		sample = *channels[ chan ];
		// Add left and right part
		//  for this channel (sound)
		//  to the current data.
		// Adjust volume accordingly.
		dl += channelleftvol_lookup[ chan ][sample];
		dr += channelrightvol_lookup[ chan ][sample];
		// Apply pitch step to offset, 16.16 fixed point.
		channelstepremainder[ chan ] += channelstep[ chan ];
		// Advance by integer part in high 16 bits.
		// To quadruple the sample-rate, we must slow down
		// the stepping speed by 4x (add 1/4 of the actual step)
		channels[ chan ] += channelstepremainder[ chan ] >> (16+SFX_STEP_SHIFT);
		// Keep remainder in low 16 bits.
		// As above, we need to keep an extra *4 (2 bits)
		channelstepremainder[ chan ] &= (1<<(16+SFX_STEP_SHIFT))-1;

		// Check whether we are done.
		if (channels[ chan ] >= channelsend[ chan ])
		    channels[ chan ] = 0;
	    }
	}

	// Write interleaved samples (left, right)
	dl = biquadlp_step(&pcm_lpf_left, dl);  // left channel
	dr = biquadlp_step(&pcm_lpf_right, dr);  // right channel

	dl += musicbuf[0];
	dr += musicbuf[1];

	if (dl > 0x7fff) {
		dl = 0x7fff;
	} else if (dl < -0x8000) {
		dl = -0x8000;
	}
	if (dr > 0x7fff) {
		dr = 0x7fff;
	} else if (dr < -0x8000) {
		dr = -0x8000;
	}

	*leftout = dl;
	*rightout = dr;

	// Increment current pointers in mixbuffer.
	leftout += MIX_CHANNELS;
	rightout += MIX_CHANNELS;
	musicbuf += MIX_CHANNELS;
    }

    Mutex_Unlock(&mix_mutex);
}

static void mix_callback( void* userdata, uint8_t* buffer, int buffer_size ) {
	int16_t* mixbuf = (int16_t*)buffer;
	int samples_needed = (buffer_size / sizeof(int16_t));
	int frames_needed = samples_needed/MIX_CHANNELS;
	if (frames_needed > mix_max_frames) {
		return; // overflows buffer
	}
	mix_music( frames_needed );
	mix_samples( mixbuf, samples_needed );
}


// --------------------------------------------------------------------------
// MIXER THREAD ENDS



//
// This function loads the sound data from the WAD lump,
//  for single sound.
// On the main thread.
//
static void*
getsfx
( char*         sfxname,
  int*          len )
{
    unsigned char*      sfx;
    unsigned char*      paddedsfx;
    int                 i;
    int                 size;
    int                 paddedsize;
    char                name[20];
    int                 sfxlump;

    
    // Get the sound data from the WAD, allocate lump
    //  in zone memory.
    sprintf(name, "ds%s", sfxname);

    // Now, there is a severe problem with the
    //  sound handling, in it is not (yet/anymore)
    //  gamemode aware. That means, sounds from
    //  DOOM II will be requested even with DOOM
    //  shareware.
    // The sound list is wired into sounds.c,
    //  which sets the external variable.
    // I do not do runtime patches to that
    //  variable. Instead, we will use a
    //  default sound for replacement.
    if ( W_CheckNumForName(name) == -1 )
      sfxlump = W_GetNumForName("dspistol");
    else
      sfxlump = W_GetNumForName(name);
    
    size = W_LumpLength( sfxlump );

    // Debug.
    // fprintf( stderr, "." );
    //fprintf( stderr, " -loading  %s (lump %d, %d bytes)\n",
    //	     sfxname, sfxlump, size );
    //fflush( stderr );
    
    sfx = (unsigned char*)W_CacheLumpNum( sfxlump, PU_STATIC );

    // Pads the sound effect out to the mixing buffer size.
    // The original realloc would interfere with zone memory.
    paddedsize = ((size-8 + (mix_max_frames-1)) / mix_max_frames) * mix_max_frames;

    // Allocate from zone memory.
    paddedsfx = (unsigned char*)Z_Malloc( paddedsize+8, PU_STATIC, 0 );
    // ddt: (unsigned char *) realloc(sfx, paddedsize+8);
    // This should interfere with zone memory handling,
    //  which does not kick in in the soundserver.

    // Now copy and pad.
    memcpy(  paddedsfx, sfx, size );
    for (i=size ; i<paddedsize+8 ; i++)
        paddedsfx[i] = 128;

    // Remove the cached lump.
    Z_Free( sfx );
    
    // Preserve padded length.
    *len = paddedsize;

    // Return allocated padded data.
    return (void *) (paddedsfx + 8);
}


//
// This function adds a sound to the
//  list of currently active sounds,
//  which is maintained as a given number
//  (eight, usually) of internal channels.
// Returns a handle.
// On the main thread with LOCK held.
//
static int addsfx_with_lock
( int		sfxid,
  int		volume,
  int		step,
  int		seperation )
{
    int		i;
    int         handle;
    
    int		oldest = gametic;
    int		oldestnum = 0;
    int		slot;

    int		rightvol;
    int		leftvol;

    // Chainsaw troubles.
    // Play these sound effects only one at a time.
    if ( sfxid == sfx_sawup
	 || sfxid == sfx_sawidl
	 || sfxid == sfx_sawful
	 || sfxid == sfx_sawhit
	 || sfxid == sfx_stnmov
	 || sfxid == sfx_pistol	 )
    {
	// Loop all channels, check.
	for (i=0 ; i<NUM_CHANNELS ; i++)
	{
	    // Active, and using the same SFX?
	    if ( (channels[i])
		 && (channelids[i] == sfxid) )
	    {
		// Reset.
		channels[i] = 0;
		// We are sure that iff,
		//  there will only be one.
		break;
	    }
	}
    }

    // Loop all channels to find oldest SFX.
    for (i=0; (i<NUM_CHANNELS) && (channels[i]); i++)
    {
	if (channelstart[i] < oldest)
	{
	    oldestnum = i;
	    oldest = channelstart[i];
	}
    }

    // Tales from the cryptic.
    // If we reached the end, all channels were playing, oldestnum is the oldest.
    // If not, we found a channel that wasn't in use and stopped early.
    if (i == NUM_CHANNELS)
	slot = oldestnum;
    else
	slot = i;

    // Okay, in the less recent channel,
    //  we will handle the new SFX.
    // Set pointer to raw data.
    channels[slot] = (unsigned char *) sfx_data[sfxid];
    // Set pointer to end of raw data.
    channelsend[slot] = channels[slot] + sfx_length[sfxid];

    // Set stepping (pitch)
    channelstep[slot] = step;
    // Initial offset in sample.
    channelstepremainder[slot] = 0;
    // Should be gametic, I presume.
    channelstart[slot] = gametic;

    // Separation, that is, orientation/stereo.
    //  range is: 1 - 256
    seperation += 1;

    // Per left/right channel.
    //  x^2 seperation,
    //  adjust volume properly.
    leftvol =
	volume - ((volume*seperation*seperation) >> 16); ///(256*256);
    seperation = seperation - 257;
    rightvol =
	volume - ((volume*seperation*seperation) >> 16);	

    // Sanity check, clamp volume.
    if (rightvol < 0 || rightvol > 127)
	I_Error("rightvol out of bounds");
    
    if (leftvol < 0 || leftvol > 127)
	I_Error("leftvol out of bounds");
    
    // Get the proper lookup table piece
    //  for this volume level.
    channelleftvol_lookup[slot] = &vol_lookup[leftvol*256];
    channelrightvol_lookup[slot] = &vol_lookup[rightvol*256];

    // Preserve sound SFX id,
    //  e.g. for avoiding duplicates of chainsaw.
    channelids[slot] = sfxid;

    // Handle is next handle number combined with slot index.
    channelhandles[slot] = nexthandle;
    handle = nexthandle | (unsigned int)slot;
    nexthandle += NUM_CHANNELS_POW2; // inc high bits above slot.
    return handle;
}


//
// SFX API
// Note: this was called by S_Init.
// However, whatever they did in the
// old DPMS based DOS version, this
// were simply dummies in the Linux
// version.
// On the main thread, before mixer thread starts. STARTS mixer.
//
void I_SetChannels()
{
  // Init internal lookups (raw data, mixing buffer, channels).
  // This function sets up internal lookups used during
  //  the mixing process. 
  int		i;
  int		j;
  int		v;
    
  int*	steptablemid = steptable + 128;

  // Okay, reset internal mixing channels to zero.
  for (i=0; i<NUM_CHANNELS; i++)
  {
    channels[i] = 0;
  }

  // This table provides step widths for pitch parameters.
  // I fail to see that this is currently used.
  for (i=-128 ; i<128 ; i++)
    steptablemid[i] = (int)(pow(2.0, (i/64.0))*65536.0);
  
  
  // Generates volume lookup tables
  //  which also turn the unsigned samples
  //  into signed samples.
  for (i=0 ; i<128 ; i++) {
    v = (i*i)>>7; // log curve
    for (j=0 ; j<256 ; j++)
      vol_lookup[i*256+j] = (v*(j-128)*256)/127;
  }

  // Find the GENMIDI lump and register instruments.
  int op2lump = W_CheckNumForName("GENMIDI.OP2");
  if ( op2lump == -1 )
    op2lump = W_GetNumForName("GENMIDI");
  char *op2 = W_CacheLumpNum( op2lump, PU_STATIC );
  musplay_op2bank(&music_driver.player, op2+8); // skip "#OPL_II#" to get BYTE[175][36] instrument data
  Z_Free( op2 );

  // Initialise audio.
  biquadlp_init(&pcm_lpf_left, MIX_SAMPLERATE, PCM_CUTOFF_HZ, PCM_Q_FACTOR);
  biquadlp_init(&pcm_lpf_right, MIX_SAMPLERATE, PCM_CUTOFF_HZ, PCM_Q_FACTOR);

  // Start audio.
  // CONCURRENCY: starts mixer thread, full memory barrier.
  Audio_Start(ddev_sound);
}



// Set sound effects mixer volume.
// On the main thread (API)
void I_SetSfxVolume(int volume) // 0-127
{
  // Identical to DOS.
  // Basically, this should propagate
  //  the menu/config file setting
  //  to the state variable used in
  //  the mixing.
  // This is handled via I_StartSound and I_UpdateSoundParams.
}


// MUSIC API. Some code from DOS version.
// On the main thread. Acquires LOCK.
void I_SetMusicVolume(int volume) // 0-127
{
    if (volume < 0 || volume > 127)
	I_Error("Attempt to set music volume at %d", volume);

  // apply log-scaling to the requested volume.
  // we can't use vol_lookup with 16-bit samples so this will do.
  volume += 2; // a bit of boost at max volume
  volume = (volume * volume) >> 7;

  Atomic_Set_Int(&music_volume, volume);
}


//
// Retrieve the raw data lump index
//  for a given SFX name.
// On the main thread.
//
int I_GetSfxLumpNum(sfxinfo_t* sfx)
{
    char namebuf[9];
    sprintf(namebuf, "ds%s", sfx->name);
    return W_GetNumForName(namebuf);
}


//
// Starting a sound means adding it
//  to the current list of active sounds
//  in the internal channels.
// As the SFX info struct contains
//  e.g. a pointer to the raw data,
//  it is ignored.
// As our sound handling does not handle
//  priority, it is ignored.
// Pitching (that is, increased speed of playback)
//  is set, but currently not used by mixing.
//
// On the main thread. Acquires LOCK.
//
int
I_StartSound
( int		id,
  int		vol,
  int		sep,
  int		pitch,
  int		priority )
{
  int           handle;

	Mutex_Lock(&mix_mutex);

	// Returns a handle, later used for I_UpdateSoundParams
	// Assumes volume in 0..127
	handle = addsfx_with_lock( id, vol, steptable[pitch], sep );

	// fprintf( stderr, "/handle is %d\n", id );

	Mutex_Unlock(&mix_mutex);

  // UNUSED
  priority = 0;
    
  return handle;
}


// On the main thread. Acquires LOCK.
void I_StopSound (int handle)
{
  unsigned int h = handle; // modern UB.

  // You need the handle returned by StartSound.
  // Would be looping all channels,
  //  tracking down the handle,
  //  an setting the channel to zero.
  
  int slot = h & (NUM_CHANNELS_POW2-1);

  Mutex_Lock(&mix_mutex);

  // Check if the slot is still playing the same handle.
  if (channels[slot] && channelhandles[slot] == (h & ~(NUM_CHANNELS_POW2-1))) {
	// Reset.
	channels[slot] = 0;
  }

  Mutex_Unlock(&mix_mutex);
}


// On the main thread. Acquires LOCK.
int I_SoundIsPlaying(int handle)
{
  int playing = 0;
  unsigned int h = handle; // modern UB.

  int slot = h & (NUM_CHANNELS_POW2-1);

  Mutex_Lock(&mix_mutex);

  // Check if the slot is still playing the same handle.
  if (channels[slot] && channelhandles[slot] == (h & ~(NUM_CHANNELS_POW2-1))) {
	playing = 1;
  }

  Mutex_Unlock(&mix_mutex);

  return playing;
}


// Not used.
void I_UpdateSound( void )
{
  // Moved to mixer thread.
}


// 
// This would be used to write out the mixbuffer
//  during each game loop update.
// Updates sound buffer and audio device at runtime. 
// Mixing now done synchronous, and
//  only output be done asynchronous?
//
void
I_SubmitSound(void)
{
  // Moved to mixer thread.
}


// On the main thread. Acquires LOCK.
void
I_UpdateSoundParams
( int	handle,
  int	volume,
  int	seperation,
  int	pitch)
{
  int		rightvol;
  int		leftvol;

  unsigned int h = handle; // modern UB.

  // Use the handle to identify
  //  on which channel the sound might be active,
  //  and reset the channel parameters.

  int slot = h & (NUM_CHANNELS_POW2-1);

  Mutex_Lock(&mix_mutex);

  // Check if the slot is still playing the same handle.
  if (channels[slot] && channelhandles[slot] == (h & ~(NUM_CHANNELS_POW2-1))) {
	
    // Set stepping (pitch)
    channelstep[slot] = steptable[pitch];

    // Separation, that is, orientation/stereo.
    //  range is: 1 - 256
    seperation += 1;

    // Per left/right channel.
    //  x^2 seperation,
    //  adjust volume properly.
    leftvol =
	volume - ((volume*seperation*seperation) >> 16); ///(256*256);
    seperation = seperation - 257;
    rightvol =
	volume - ((volume*seperation*seperation) >> 16);	

    // Sanity check, clamp volume.
    if (rightvol < 0 || rightvol > 127)
	I_Error("rightvol out of bounds");
    
    if (leftvol < 0 || leftvol > 127)
	I_Error("leftvol out of bounds");
    
    // Get the proper lookup table piece
    //  for this volume level.
    channelleftvol_lookup[slot] = &vol_lookup[leftvol*256];
    channelrightvol_lookup[slot] = &vol_lookup[rightvol*256];

  }

   Mutex_Unlock(&mix_mutex);
}




// On the main thread. STOPS MIXER.
void I_ShutdownSound(void)
{    
  // Wait till all pending sounds are finished.
  // int done = 0;
  // int i;
  

  // FIXME (below).
  fprintf( stderr, "I_ShutdownSound: NOT finishing pending sounds\n");
  fflush( stderr );
  
  // while ( !done )
  // {
  //   for( i=0 ; i<8 && !channels[i] ; i++);
  //   
  //   // FIXME. No proper channel output.
  //   //if (i==8)
  //   done=1;
  // }

  // Stop the audio mixer and mixer thread.  
  Audio_Stop(ddev_sound);

  // Release the Audio device.
  System_DropCapability(ddev_sound);

  // Done.
  return;
}


// On the main thread, before mixer starts.
void
I_InitSound()
{ 
  int i;
  
  // Secure and configure sound device first.
  fprintf( stderr, "I_InitSound: ");

  Mutex_Init(&mix_mutex);

  Audio_CreateStream(ddev_sound, mix_callback, Audio_Fmt_S16, MIX_CHANNELS, MIX_SAMPLERATE, MIX_CHUNK_SIZE);
  mix_max_frames = Audio_FrameCount(ddev_sound); // init once

  uint32_t buf_size = musdriver_opl_buf_size(MIX_SAMPLERATE, mix_max_frames);
  void* oplbuf = Buffer_Create(ddev_musicbuf, buf_size, 0);
  musdriver_init(&music_driver, oplbuf, MIX_SAMPLERATE, mix_max_frames, OPL_CUTOFF_HZ);
  music_downmix = Buffer_Create(ddev_musicmix, mix_max_frames*sizeof(int16_t)*MIX_CHANNELS, 0);

  // Initialize external data (all sounds) at start, keep static.
  fprintf( stderr, "I_InitSound: sfx_max=%d opl_max=%d\n", (int)mix_max_frames, (int)music_driver.opl_max_frames);

  for (i=1 ; i<NUMSFX ; i++)
  { 
    // Alias? Example is the chaingun sound linked to pistol.
    if (!S_sfx[i].link)
    {
      // Load data from WAD file.
      S_sfx[i].data = sfx_data[i] = getsfx( S_sfx[i].name, &sfx_length[i] );
    }	
    else
    {
      // Previously loaded already?
      S_sfx[i].data = sfx_data[i] = S_sfx[i].link->data;
      sfx_length[i] = sfx_length[(S_sfx[i].link - S_sfx)/sizeof(sfxinfo_t)];
    }
  }

  fprintf( stderr, " pre-cached all sound data\n");

  // Finished initialization.
  fprintf(stderr, "I_InitSound: sound module ready\n");
}




//
// MUSIC API.
//
void I_InitMusic(void)		{ }
void I_ShutdownMusic(void)	{ }

// Only used here to communicate between I_RegisterSong and I_PlaySong
static void*    last_registered_song = 0;
static int      started_playing = 0;

void I_PlaySong(int handle, int loop)
{
  // UNUSED.
  handle = 0;

  if (last_registered_song) {
	started_playing = 1;
	Atomic_Set_Int(&music_loop, loop); // prior to release
	Atomic_Set_Ptr_Release(&music_songptr, last_registered_song);
  }
}

void I_PauseSong (int handle)
{
  // UNUSED.
  handle = 0;

  Atomic_Set_Int(&music_paused, 1);
}

void I_ResumeSong (int handle)
{
  // UNUSED.
  handle = 0;

  Atomic_Set_Int(&music_paused, 0);
}

void I_StopSong(int handle)
{
  // UNUSED.
  handle = 0;

  started_playing = 0;
  Atomic_Set_Ptr(&music_songptr, 0);
}

void I_UnRegisterSong(int handle)
{
  // UNUSED.
  handle = 0;
}

int I_RegisterSong(void* data)
{
  // Always registered just before I_PlaySong.
  // Always unregistered just after I_StopSong.
  // Music lump data. Returns handle.
  last_registered_song = data;
  return 1;
}

// Is the song playing?
int I_QrySongPlaying(int handle)
{
  // UNUSED.
  handle = 0;

  return started_playing && Atomic_Get_Ptr(&music_finished) != last_registered_song;
}
