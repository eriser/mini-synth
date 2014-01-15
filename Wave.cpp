/*
MINI VIRTUAL ANALOG SYNTHESIZER
Copyright 2014 Kenneth D. Miller III

Wave Types
*/
#include "StdAfx.h"

#include "Wave.h"
#include "WaveSine.h"
#include "WavePulse.h"
#include "WaveSawtooth.h"
#include "WaveTriangle.h"
#include "WaveNoise.h"
#include "WavePoly.h"

// waveform antialiasing
bool use_antialias = true;

// map wave type enumeration to oscillator function
OscillatorFunc const oscillator[WAVE_COUNT] =
{
	OscillatorSine,			// WAVE_SINE,
	OscillatorPulse,		// WAVE_PULSE,
	OscillatorSawtooth,		// WAVE_SAWTOOTH,
	OscillatorTriangle,		// WAVE_TRIANGLE,
	OscillatorNoise,		// WAVE_NOISE,
	OscillatorPoly4,		// WAVE_POLY4,
	OscillatorPoly5,		// WAVE_POLY5,
	OscillatorPeriod93,		// WAVE_PERIOD93,
	OscillatorPoly9,		// WAVE_POLY9,
	OscillatorPoly17,		// WAVE_POLY17,
	OscillatorPulsePoly5,	// WAVE_PULSE_POLY5,
	OscillatorPoly4Poly5,	// WAVE_POLY4_POLY5,
	OscillatorPoly17Poly5	// WAVE_POLY17_POLY5,
};

// names for wave types
char const * const wave_name[WAVE_COUNT] =
{
	"Sine",			// WAVE_SINE,
	"Pulse",		// WAVE_PULSE,
	"Sawtooth",		// WAVE_SAWTOOTH,
	"Triangle",		// WAVE_TRIANGLE,
	"Noise",		// WAVE_NOISE,
	"Poly4",		// WAVE_POLY4,
	"Poly5",		// WAVE_POLY5,
	"Period-93",	// WAVE_PERIOD93,
	"Poly9",		// WAVE_POLY9,
	"Poly17",		// WAVE_POLY17,
	"Pulse/Poly5",	// WAVE_PULSE_POLY5,
	"Poly4/Poly5",	// WAVE_POLY4_POLY5,
	"Poly17/Poly5"	// WAVE_POLY17_POLY5,
};

// multiply oscillator time scale based on wave type
// - tune the pitch of short-period poly oscillators
// - raise the pitch of poly oscillators by a factor of two
// - Atari POKEY pitch 255 corresponds to key 9 (N) in octave 2
float const wave_adjust_frequency[WAVE_COUNT] =
{
	1.0f,					// WAVE_SINE,
	1.0f,					// WAVE_PULSE,
	1.0f,					// WAVE_SAWTOOTH,
	1.0f,					// WAVE_TRIANGLE,
	1.0f, 					// WAVE_NOISE,
	2.0f * 15.0f / 16.0f,	// WAVE_POLY4,
	2.0f * 31.0f / 32.0f,	// WAVE_POLY5,
	2.0f * 93.0f / 128.0f,	// WAVE_PERIOD93,
	2.0f * 511.0f / 512.0f,	// WAVE_POLY9,
	2.0f,					// WAVE_POLY17,
	2.0f * 31.0f / 32.0f,	// WAVE_PULSE_POLY5,
	2.0f * 465.0f / 512.0f,	// WAVE_POLY4_POLY5,
	2.0f,					// WAVE_POLY17_POLY5,
};

// restart the oscillator loop index after this many phase cycles
// - poly oscillators using the loop index to look up precomputed values
int const wave_loop_cycle[WAVE_COUNT] =
{
	INT_MAX,					// WAVE_SINE,
	INT_MAX,					// WAVE_PULSE,
	INT_MAX,					// WAVE_SAWTOOTH,
	INT_MAX,					// WAVE_TRIANGLE,
	INT_MAX,					// WAVE_NOISE,
	ARRAY_SIZE(poly4),			// WAVE_POLY4,
	ARRAY_SIZE(poly5),			// WAVE_POLY5,
	ARRAY_SIZE(period93),		// WAVE_PERIOD93,
	ARRAY_SIZE(poly9),			// WAVE_POLY9,
	ARRAY_SIZE(poly17),			// WAVE_POLY17,
	ARRAY_SIZE(pulsepoly5),		// WAVE_PULSE_POLY5,
	ARRAY_SIZE(poly4poly5),		// WAVE_POLY4_POLY5,
	ARRAY_SIZE(poly17poly5),	// WAVE_POLY17_POLY5,
};
