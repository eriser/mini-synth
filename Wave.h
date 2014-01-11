#pragma once
/*
MINI VIRTUAL ANALOG SYNTHESIZER
Copyright 2014 Kenneth D. Miller III

Wave Types
*/

// waveform antialiasing
#define ANTIALIAS_NONE 0
#define ANTIALIAS_POLYBLEP 1
#define ANTIALIAS 1
extern bool use_antialias;

// oscillator wave types
enum Wave
{
	WAVE_SINE,
	WAVE_PULSE,
	WAVE_SAWTOOTH,
	WAVE_TRIANGLE,
	WAVE_NOISE,

	WAVE_POLY4,			// POKEY AUDC 12
	WAVE_POLY5,
	WAVE_POLY17,		// POKEY AUDC 8
	WAVE_PULSE_POLY5,	// POKEY AUDC 2, 6
	WAVE_POLY4_POLY5,	// POKEY AUDC 4
	WAVE_POLY17_POLY5,	// POKEY AUDC 0

	WAVE_COUNT
};

// oscillator wave function
class OscillatorConfig;
class OscillatorState;
typedef float(*OscillatorFunc)(OscillatorConfig const &config, OscillatorState &state, float step);

// map wave type enumeration to oscillator function
extern OscillatorFunc const oscillator[WAVE_COUNT];

// names for wave types
extern char const * const wave_name[WAVE_COUNT];

// multiply oscillator time scale based on wave type
extern float const wave_adjust_frequency[WAVE_COUNT];

// restart the oscillator loop index after this many phase cycles
extern int const wave_loop_cycle[WAVE_COUNT];
