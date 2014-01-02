/*
MINI VIRTUAL ANALOG SYNTHESIZER
Derived from BASS simple synth demo
*/

#include <windows.h>
#include <stdio.h>
#include <conio.h>
#include <math.h>
#include <float.h>
#include <assert.h>
#include "bass.h"

BASS_INFO info;
HSTREAM stream; // the stream

#define FILTER_IMPROVED_MOOG 0
#define FILTER_NONLINEAR_MOOG 1
#define FILTER 0

// debug output
int DebugPrint(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
#ifdef WIN32
	char buf[4096];
	int n = vsnprintf(buf, sizeof(buf), format, ap);
	OutputDebugStringA(buf);
#else
	int n = vfprintf(stderr, format, ap);
#endif
	va_end(ap);
	return n;
}

// formatted write console output
int PrintConsole(HANDLE out, COORD pos, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	char buf[256];
	int n = vsnprintf(buf, sizeof(buf), format, ap);
	DWORD written;
	WriteConsoleOutputCharacter(out, buf, strlen(buf), pos, &written);
	return written;
}

// display error messages
void Error(char const *text)
{
	fprintf(stderr, "Error(%d): %s\n", BASS_ErrorGetCode(), text);
	BASS_Free();
	ExitProcess(0);
}

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

template<typename T> static inline T Clamp(T x, T a, T b)
{
	if (x < a)
		return a;
	if (x > b)
		return b;
	return x;
}
template<typename T> static inline T Min(T a, T b)
{
	return a < b ? a : b;
}
template<typename T> static inline T Max(T a, T b)
{
	return a > b ? a : b;
}

static inline float Saturate(float input)
{
	return 0.5f * (fabsf(input + 1.0f) - fabsf(input - 1.0f));
}


char const title_text[] = ">>> MINI VIRTUAL ANALOG SYNTHESIZER";
COORD const title_pos = { 0, 0 };

WORD const keys[] = {
	'Z', 'S', 'X', 'D', 'C', 'V', 'G', 'B', 'H', 'N', 'J', 'M',
	'Q', '2', 'W', '3', 'E', 'R', '5', 'T', '6', 'Y', '7', 'U',
};
COORD const key_pos = { 0, 6 };
//{
//	{ 1, 5 }, { 2, 4 }, { 3, 5 }, { 4, 4 }, { 5, 5 }, { 7, 5 }, { 8, 4 }, { 9, 5 }, { 10, 4 }, { 11, 5 }, { 12, 4 }, { 13, 5 },
//	{ 1, 2 }, { 2, 1 }, { 3, 2 }, { 4, 1 }, { 5, 2 }, { 7, 2 }, { 8, 1 }, { 9, 2 }, { 10, 1 }, { 11, 2 }, { 12, 1 }, { 13, 2 },
//	{ 1, 2 }, { 2, 1 }, { 3, 2 }, { 4, 1 }, { 5, 2 }, { 7, 2 }, { 8, 1 }, { 9, 2 }, { 10, 1 }, { 11, 2 }, { 12, 1 }, { 13, 2 },
//	{ 15, 2 }, { 16, 1 }, { 17, 2 }, { 18, 1 }, { 19, 2 }, { 21, 2 }, { 22, 1 }, { 23, 2 }, { 24, 1 }, { 25, 2 }, { 26, 1 }, { 27, 2 },
//};
static size_t const KEYS = ARRAY_SIZE(keys);

enum MenuMode
{
	MENU_LFO,
	MENU_OSC,
	MENU_FLT,
	MENU_VOL,
	MENU_FX,

	MENU_COUNT
};

WORD const menu_title_attrib[] =
{
	FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | BACKGROUND_BLUE,
	FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY | BACKGROUND_GREEN | BACKGROUND_BLUE,
};
WORD const menu_item_attrib[] =
{
	FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,
	FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY | BACKGROUND_BLUE,
};

// selected menu
MenuMode menu_active = MENU_FLT;

// selected item in each menu
int menu_item[MENU_COUNT];

char const * const fx_name[9] =
{
	"Chorus", "Compressor", "Distortion", "Echo", "Flanger", "Gargle", "I3DL2Reverb", "ParamEQ", "Reverb"
};

// effect handles
HFX fx[9];

// keyboard base frequencies
float keyboard_frequency[KEYS];

// keyboard octave
int keyboard_octave = 4;
float keyboard_timescale = 1.0f;

// oscillator wave types
enum Wave
{
	WAVE_SINE,
	WAVE_PULSE,
	WAVE_SAWTOOTH,
	WAVE_TRIANGLE,
	WAVE_POLY4,
	WAVE_POLY5,
	WAVE_POLY17,
	WAVE_NOISE,

	WAVE_COUNT
};

// precomputed linear feedback shift register output
static char poly4[(1 << 4) - 1];
static char poly5[(1 << 5) - 1];
static char poly17[(1 << 17) - 1];

// oscillator update function
typedef float(*OscillatorFunc)(float amplitude, float phase, int loops, float param);

// low frequency oscillator configuration
struct LFOConfig
{
	Wave wavetype;
	float waveparam;
	float frequency;
};
LFOConfig lfo_config = { WAVE_SINE, 0.5f, 1.0f };
// TO DO: multiple LFOs?
// TO DO: LFO key sync
// TO DO: LFO temp sync?
// TO DO: LFO keyboard follow dial?

// low frequency oscillator state
struct LFOState
{
	float phase;
	float loops;

	float Update(LFOConfig const &config, float const step);
};
LFOState lfo_state;

// oscillator configuration
struct OscillatorConfig
{
	// editable properties
	Wave wavetype;
	float waveparam_base;
	float frequency_base;	// relative to key
	float amplitude_base;

	// LFO modulation parameters
	float waveparam_lfo;
	float frequency_lfo;
	float amplitude_lfo;

	// shared state
	float waveparam;
	float frequency;
	float amplitude;
};
OscillatorConfig osc_config = { WAVE_SINE, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f };
// TO DO: multiple oscillators
// TO DO: hard sync
// TO DO: keyboard follow dial?
// TO DO: oscillator mixer

// oscillator state
struct OscillatorState
{
	float phase;
	float loops;

	float Update(OscillatorConfig const &config, float const step);
};
OscillatorState osc_state[KEYS];

// envelope generator configuration
struct EnvelopeConfig
{
	float attack_rate;
	float decay_rate;
	float sustain_level;
	float release_rate;
};
EnvelopeConfig flt_env_config = { 256.0f, 16.0f, 0.0f, 256.0f };
EnvelopeConfig vol_env_config = { 256.0f, 16.0f, 1.0f, 256.0f };

// envelope generator state
struct EnvelopeState
{
	bool gate;
	enum State
	{
		OFF,

		ATTACK,
		DECAY,
		SUSTAIN,
		RELEASE,

		COUNT
	};
	State state;
	float amplitude;

	float Update(EnvelopeConfig const &config, float const step);
};

EnvelopeState flt_env_state[KEYS];
EnvelopeState vol_env_state[KEYS];
static float const ENV_ATTACK_BIAS = 1.0f/(1.0f-expf(-1.0f))-1.0f;	// 1x time constant
static float const ENV_DECAY_BIAS = 1.0f-1.0f/(1.0f-expf(-3.0f));	// 3x time constant
// TO DO: multiple envelopes?  filter, amplifier, global

WORD const env_attrib[EnvelopeState::COUNT] =
{
	FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED,	// EnvelopeState::OFF
	BACKGROUND_GREEN | BACKGROUND_INTENSITY,				// EnvelopeState::ATTACK,
	BACKGROUND_RED | BACKGROUND_INTENSITY,					// EnvelopeState::DECAY,
	BACKGROUND_BLUE | BACKGROUND_GREEN | BACKGROUND_RED,	// EnvelopeState::SUSTAIN,
	BACKGROUND_INTENSITY,									// EnvelopeState::RELEASE,
};


// resonant lowpass filter
struct FilterConfig
{
	enum Mode
	{
		NONE,
		ALLPASS,
		LOWPASS_1,
		LOWPASS_2,
		LOWPASS_3,
		LOWPASS_4,
		HIGHPASS_1,
		HIGHPASS_2,
		HIGHPASS_3,
		HIGHPASS_4,
		BANDPASS,
		NOTCH,

		COUNT
	};
	Mode mode;
	float cutoff;
	float cutoff_lfo;
	float cutoff_env;
	float resonance;
};
FilterConfig flt_config = { FilterConfig::NONE, 0.0f, 0.0f, 0.0f, 0.0f };

const char * const filter_name[FilterConfig::COUNT] =
{
	"None", "All-Pass",
	"Low-Pass 1", "Low-Pass 2", "Low-Pass 3", "Low-Pass 4",
	"High-Pass 1", "High-Pass 2", "High-Pass 3", "High-Pass 4",
	"Band-Pass", "Notch"
};

// filter state
struct FilterState
{
#if FILTER == FILTER_NONLINEAR_MOOG

#define FILTER_OVERSAMPLE 2

	float feedback;
	float tune;
	float ytan[4];
	float y[5];

#else

#define FILTER_OVERSAMPLE 2

	// feedback coefficient
	float feedback;

	// filter stage IIR coefficients
	// H(z) = (b0 * z + b1) / (z + a1)
	// H(z) = (b0 + b1 * z^-1) / (1 + a1 * z-1)
	// H(z) = Y(z) / X(z)
	// Y(z) = b0 + b1 * z^-1
	// X(z) = 1 + a1 * z^-1
	// (1 + a1 * z^-1) * Y(z) = (b0 + b1 * z^-1) * X(z)
	// y[n] + a1 * y[n - 1] = b0 * x[n] + b1 * x[n - 1]
	// y[n] = b0 * x[n] + b1 * 0.3 * x[n-1] - a1 * y[n-1]
	float b0, b1, a1;

	// output values from each stage
	// (y[0] is input to the first stage)
	float y[5];

#endif

	void Clear(void);
	void Setup(float cutoff, float resonance);
	float Apply(float input);
};
FilterState flt_state[KEYS];

// output scale factor
float output_scale = 1.0f;	// 0.25f;

// from Atari800 pokey.c
static void InitPoly(char aOut[], int aSize, int aTap, unsigned int aSeed, char aInvert)
{
	unsigned int x = aSeed;
	unsigned int i = 0;
	do
	{
		aOut[i] = (x & 1) ^ aInvert;
		x = ((((x >> aTap) ^ x) & 1) << (aSize - 1)) | (x >> 1);
		++i;
	}
	while (x != aSeed);
}

// names for wave types
char const * const wave_name[WAVE_COUNT] =
{
	"Sine", "Pulse", "Sawtooth", "Triangle", "Poly4", "Poly5", "Poly17", "Noise"
};

// multiply oscillator time scale based on wave type
// - tune the pitch of short-period poly oscillators
// - raise the pitch of poly oscillators by two octaves
float const wave_adjust_frequency[WAVE_COUNT] =
{
	1.0f, 1.0f, 1.0f, 1.0f, 4.0f * 15 / 16, 4.0f * 31 / 32, 4.0f, 1.0f
};

// restart the oscillator loop index after this many phase cycles
// - poly oscillators using the loop index to look up precomputed values
int const wave_loop_cycle[WAVE_COUNT] =
{
	1, 1, 1, 1, ARRAY_SIZE(poly4), ARRAY_SIZE(poly5), ARRAY_SIZE(poly17), 1
};

// sine oscillator
float OscillatorSine(float amplitude, float phase, int loops, float param)
{
	return amplitude * sinf(M_PI * 2.0f * phase);
}

// pulse oscillator
// - param controls pulse width
// - pulse width 0.5 is square wave
// - sum k=0..infinity sin((2*k+1)*2*pi*phase)/(2*k+1)
float OscillatorPulse(float amplitude, float phase, int loops, float param)
{
	return phase < param ? amplitude : -amplitude;
	//return amplitude * 2.0f * (ceilf(phase) - roundf(phase));
}

// sawtooth oscillator
// - sum k=1..infinity sin(k*2*pi*phase)/n
float OscillatorSawtooth(float amplitude, float phase, int loops, float param)
{
#if 1
	return amplitude * (1.0f - 2.0f * phase);
#else
	if (phase < 0.5f)
		return amplitude * 2.0f * phase;
	else
		return amplitude * 2.0f * (phase - 1.0f);
#endif
}

// triangle oscillator
// - sum k=0..infinity (-1)**k sin((2*k+1)*2*pi*phase)/(2*k+1)**2
float OscillatorTriangle(float amplitude, float phase, int loops, float param)
{
	if (phase < 0.25f)
		return amplitude * 4.0f * phase;
	else if (phase < 0.75f)
		return amplitude * 4.0f * (0.5f - phase);
	else
		return amplitude * 4.0f * (phase - 1.0f);
	//return amplitude * (4.0f * fabsf(phase + 0.25f - roundf(phase + 0.25f)) - 1.0f);
}

// poly4 oscillator
// 4-bit linear feedback shift register noise
float OscillatorPoly4(float amplitude, float phase, int loops, float param)
{
	return poly4[loops] ? amplitude : -amplitude;
}

// poly5 oscillator
// 5-bit linear feedback shift register noise
float OscillatorPoly5(float amplitude, float phase, int loops, float param)
{
	return poly5[loops] ? amplitude : -amplitude;
}

// poly17 oscillator
// 17-bit linear feedback shift register noise
float OscillatorPoly17(float amplitude, float phase, int loops, float param)
{
	return poly17[loops] ? amplitude : -amplitude;
}

namespace Random
{
	// random seed
	unsigned int gSeed = 0x92D68CA2;

	// set seed
	inline void Seed(unsigned int aSeed)
	{
		gSeed = aSeed;
	}

	// random unsigned long
	inline unsigned int Int()
	{
#if 1
		gSeed ^= (gSeed << 13);
		gSeed ^= (gSeed >> 17);
		gSeed ^= (gSeed << 5);
#else
		gSeed = 1664525L * gSeed + 1013904223L;
#endif
		return gSeed;
	}

	// random uniform float
	inline float Float()
	{
		union { float f; unsigned u; } floatint;
		floatint.u = 0x3f800000 | (Int() >> 9);
		return floatint.f - 1.0f;
	}

	// random range value
	inline float Value(float aAverage, float aVariance)
	{
		return (2.0f * Float() - 1.0f) * aVariance + aAverage;
	}
}

// white noise oscillator
float OscillatorNoise(float amplitude, float phase, int loops, float param)
{
	return Random::Value(0.0f, 2.0f * amplitude);
	//return amplitude * 2.0f * (Random::Float() - Random::Float());
}

// map wave type enumeration to oscillator function
OscillatorFunc const oscillator[WAVE_COUNT] =
{
	OscillatorSine, OscillatorPulse, OscillatorSawtooth, OscillatorTriangle, OscillatorPoly4, OscillatorPoly5, OscillatorPoly17, OscillatorNoise
};

// update low-frequency oscillator
float LFOState::Update(LFOConfig const &config, float const step)
{
	// get oscillator value
	float const value = oscillator[config.wavetype](1.0f, phase, loops, config.waveparam);

	// accumulate oscillator phase
	phase += config.frequency * step;
	while (phase >= 1.0f)
	{
		phase -= 1.0f;
		if (++loops >= wave_loop_cycle[config.wavetype])
			loops = 0;
	}

	return value;
}

// update oscillator
float OscillatorState::Update(OscillatorConfig const &config, float const step)
{
	// get oscillator value
	float const value = oscillator[config.wavetype](config.amplitude, phase, loops, config.waveparam);

	// accumulate oscillator phase
	phase += config.frequency * step;
	while (phase >= 1.0f)
	{
		phase -= 1.0f;
		if (++loops >= wave_loop_cycle[config.wavetype])
			loops = 0;
	}

	return value;
}

// update envelope generator
float EnvelopeState::Update(EnvelopeConfig const &config, float const step)
{
	float env_target;
	switch (state)
	{
	case ATTACK:
		env_target = 1.0f + ENV_ATTACK_BIAS;
		amplitude += (env_target - amplitude) * config.attack_rate * step;
		if (amplitude >= 1.0f)
		{
			amplitude = 1.0f;
			if (config.sustain_level < 1.0f)
				state = DECAY;
			else
				state = SUSTAIN;
		}
		break;

	case DECAY:
		env_target = config.sustain_level + (1.0f - config.sustain_level) * ENV_DECAY_BIAS;
		amplitude += (env_target - amplitude) * config.decay_rate * step;
		if (amplitude <= config.sustain_level)
		{
			amplitude = config.sustain_level;
			state = SUSTAIN;
		}
		break;

	case RELEASE:
		env_target = ENV_DECAY_BIAS;
		if (amplitude < config.sustain_level || config.decay_rate < config.release_rate)
			amplitude += (env_target - amplitude) * config.release_rate * step;
		else
			amplitude += (env_target - amplitude) * config.decay_rate * step;
		if (amplitude <= 0.0f)
		{
			amplitude = 0.0f;
			state = OFF;
		}
		break;
	}

	return amplitude;
}

void FilterState::Clear()
{
#if FILTER == FILTER_NONLINEAR_MOOG
	feedback = 0.0f;
	tune = 0.0f;
	memset(ytan, 0, sizeof(ytan));
	memset(y, 0, sizeof(y));
#else
	feedback = 0.0f;
	a1 = 0.0f; b0 = 0.0f; b1 = 0.0f;
	memset(y, 0, sizeof(y));
#endif
}

void FilterState::Setup(float cutoff, float resonance)
{
#if FILTER == FILTER_IMPROVED_MOOG

	// Based on Improved Moog Filter description
	// http://www.music.mcgill.ca/~ich/research/misc/papers/cr1071.pdf

	float const fn = FILTER_OVERSAMPLE * 0.5f * info.freq;
	float const fc = cutoff < fn ? cutoff / fn : 1.0f;
	float const g = 1 - exp(-M_PI * fc);
	feedback = 4.0f * resonance;
	// y[n] = ((1.0 / 1.3) * x[n] + (0.3 / 1.3) * x[n-1] - y[n-1]) * g + y[n-1]
	// y[n] = (g / 1.3) * x[n] + (g * 0.3 / 1.3) * x[n-1] - (g - 1) * y[n-1]
	a1 = g - 1.0f; b0 = g * 0.769231f; b1 = b0 * 0.3f;

#elif FILTER == FILTER_NONLINEAR_MOOG

	// Based on Antti Huovilainen's non-linear digital implementation
	// http://dafx04.na.infn.it/WebProc/Proc/P_061.pdf
	// https://raw.github.com/ddiakopoulos/MoogLadders/master/Source/Huovilainen.cpp
	// 0 <= resonance <= 1

	float const fn = FILTER_OVERSAMPLE * 0.5f * info.freq;
	float const fc = cutoff < fn ? cutoff / fn : 1.0f;
	float const fcr = ((1.8730f * fc + 0.4955f) * fc + -0.6490f) * fc + 0.9988f;
	float const acr = (-3.9364f * fc + 1.8409f) * fc + 0.9968f;
	feedback = resonance * 4.0f * acr;
	tune = (1.0f - expf(-M_PI * fc * fcr)) * 1.22070313f;

#endif
}

float FilterState::Apply(float input)
{
#if FILTER == FILTER_IMPROVED_MOOG

#if FILTER_OVERSAMPLE > 1
	for (int i = 0; i < FILTER_OVERSAMPLE; ++i)
#endif
	{
		// feedback
		float const comp = 0.5f;
		float const in = input - feedback * (tanh(y[4]) - comp * input);

		// stage 1: x1[n-1] = x1[n]; x1[n] = in;    y1[n-1] = y1[n]; y1[n] = func(y1[n-1], x1[n], x1[n-1])
		// stage 2: x2[n-1] = x2[n]; x2[n] = y1[n]; y2[n-1] = y2[n]; y2[n] = func(y2[n-1], x2[n], x2[n-1])
		// stage 3: x3[n-1] = x3[n]; x3[n] = y2[n]; y3[n-1] = y3[n]; y3[n] = func(y3[n-1], x3[n], x3[n-1])
		// stage 4: x4[n-1] = x4[n]; x4[n] = y3[n]; y4[n-1] = y4[n]; y4[n] = func(y4[n-1], x4[n], x4[n-1])

		// stage 1: x1[n-1] = x1[n]; x1[n] = in;    t1 = y1[n-1]; y1[n-1] = y1[n]; y1[n] = func(y1[n-1], x1[n], x1[n-1])
		// stage 2: x2[n-1] = t1;    x2[n] = y1[n]; t2 = y2[n-1]; y2[n-1] = y2[n]; y2[n] = func(y2[n-1], x2[n], x2[n-1])
		// stage 3: x3[n-1] = t2;    x3[n] = y2[n]; t3 = y3[n-1]; y3[n-1] = y3[n]; y3[n] = func(y3[n-1], x3[n], x3[n-1])
		// stage 4: x4[n-1] = t3;    x4[n] = y3[n];               y4[n-1] = y4[n]; y4[n] = func(y4[n-1], x4[n], x4[n-1])

		// t0 = y0[n], t1 = y1[n], t2 = y2[n], t3 = y3[n]
		// stage 0: y0[n] = in
		// stage 1: y1[n] = func(y1[n], y1[n], t0)
		// stage 2: y2[n] = func(y2[n], y1[n], t1)
		// stage 3: y3[n] = func(y3[n], y2[n], t2)
		// stage 4: y4[n] = func(y4[n], y3[n], t3)

		// four-pole low-pass filter
		float const t[4] = { y[0], y[1], y[2], y[3] };
		y[0] = in;
		y[1] = b0 * y[0] + b1 * t[0] - a1 * y[1];
		y[2] = b0 * y[1] + b1 * t[1] - a1 * y[2];
		y[3] = b0 * y[2] + b1 * t[2] - a1 * y[3];
		y[4] = b0 * y[3] + b1 * t[3] - a1 * y[4];
	}

	return y[4];

#elif FILTER == FILTER_NONLINEAR_MOOG

	// feedback
	float const in = input - feedback * y[4];

	float output;
#if FILTER_OVERSAMPLE > 1
	for (int i = 0; i < FILTER_OVERSAMPLE; ++i)
#endif
	{
		float last_stage = y[4];

		y[0] = in;
		ytan[0] = tanh(y[0] * 0.8192f);
		y[1] += tune * (ytan[0] - ytan[1]);
		ytan[1] = tanh(y[1] * 0.8192f);
		y[2] += tune * (ytan[1] - ytan[2]);
		ytan[2] = tanh(y[2] * 0.8192f);
		y[3] += tune * (ytan[2] - ytan[3]);
		ytan[3] = tanh(y[3] * 0.8192f);
		y[4] += tune * (ytan[3] - tanh(y[4] * 0.8192f));

		output = 0.5f * (y[4] + last_stage);
	}
	return output;

#endif
}

DWORD CALLBACK WriteStream(HSTREAM handle, short *buffer, DWORD length, void *user)
{
	// get active oscillators
	int index[KEYS];
	int active = 0;
	for (int k = 0; k < ARRAY_SIZE(keys); k++)
	{
		if (vol_env_state[k].state != EnvelopeState::OFF)
		{
			index[active++] = k;
		}
	}

	// number of samples
	size_t count = length / (2 * sizeof(short));

	// low-frequency oscillator loops after this many cycles
	float const lfo_loop_cycle = wave_loop_cycle[lfo_config.wavetype];

	if (active == 0)
	{
		// clear buffer
		memset(buffer, 0, length);

		// advance low-frequency oscillator
		lfo_state.Update(lfo_config, count / info.freq);

		return length;
	}

	// time step per output sample
	float const step = 1.0f / info.freq;

	// oscillator evaluation functions
	OscillatorFunc lfo_func = oscillator[lfo_config.wavetype];
	OscillatorFunc osc_func = oscillator[osc_config.wavetype];

	// for each output sample...
	for (int c = 0; c < count; ++c)
	{
		// get low-frequency oscillator value
		float const lfo = lfo_state.Update(lfo_config, step);

		// LFO wave parameter modulation
		osc_config.waveparam = osc_config.waveparam_base + osc_config.waveparam_lfo * lfo;

		// LFO frequency modulation
		osc_config.frequency = powf(2.0f, osc_config.frequency_base + osc_config.frequency_lfo * lfo) * wave_adjust_frequency[osc_config.wavetype];

		// LFO amplitude modulation
		osc_config.amplitude = osc_config.amplitude_base * powf(2.0f, osc_config.amplitude_lfo * lfo);

		// accumulated sample value
		float sample = 0.0f;

		// for each active oscillator...
		for (int i = 0; i < active; ++i)
		{
			int k = index[i];

			// key frequency (taking octave shift into account)
			float key_freq = keyboard_frequency[k] * keyboard_timescale;

			// update filter envelope generator
			float const flt_env_amplitude = flt_env_state[k].Update(flt_env_config, step);

			// update volume envelope generator
			float const vol_env_amplitude = vol_env_state[k].Update(vol_env_config, step);

			// if the envelope generator finished...
			if (vol_env_state[k].state == EnvelopeState::OFF)
			{
				// remove from active oscillators
				--active;
				index[i] = index[active];
				--i;
				continue;
			}

			// update oscillator
			// (assume key follow)
			float const osc_value = osc_state[k].Update(osc_config, step * key_freq);

			float flt_value;
			if (flt_config.mode)
			{
				// compute cutoff frequency
				// (assume key follow)
				float cutoff = key_freq * powf(2, (osc_config.frequency_base + flt_config.cutoff + flt_config.cutoff_lfo * lfo + flt_config.cutoff_env * flt_env_amplitude) / 12.0f);

				// compute filter values
				flt_state[k].Setup(cutoff, flt_config.resonance);

				// accumulate the filtered oscillator value
#if 1
				flt_state[k].Apply(osc_value);
				switch (flt_config.mode)
				{
				case FilterConfig::ALLPASS:		flt_value = flt_state[k].y[0]; break;
				case FilterConfig::LOWPASS_1:	flt_value = flt_state[k].y[1]; break;
				case FilterConfig::LOWPASS_2:	flt_value = flt_state[k].y[2]; break;
				case FilterConfig::LOWPASS_3:	flt_value = flt_state[k].y[3]; break;
				case FilterConfig::LOWPASS_4:	flt_value = flt_state[k].y[4]; break;
				case FilterConfig::HIGHPASS_1:	flt_value = flt_state[k].y[0] - flt_state[k].y[4]; break;
				case FilterConfig::HIGHPASS_2:	flt_value = flt_state[k].y[0] - flt_state[k].y[3]; break;
				case FilterConfig::HIGHPASS_3:	flt_value = flt_state[k].y[0] - flt_state[k].y[2]; break;
				case FilterConfig::HIGHPASS_4:	flt_value = flt_state[k].y[0] - flt_state[k].y[1]; break;
				case FilterConfig::BANDPASS:	flt_value = 2.0f * (flt_state[k].y[4] - flt_state[k].y[2]); break;
				case FilterConfig::NOTCH:		flt_value = 0.666667f * osc_value + (flt_state[k].y[4] - flt_state[k].y[2]); break;
				}
#else
				flt_value = flt_state[k].Apply(osc_value);
#endif
			}
			else
			{
				// pass unfiltered value
				flt_value = osc_value;
			}

			// apply envelope to amplitude and accumulate result
			sample += flt_value * vol_env_amplitude;
		}

		// left and right channels are the same
		//short output = (short)Clamp(sample * output_scale * 32768, SHRT_MIN, SHRT_MAX);
		short output = (short)(tanhf(sample * output_scale) * 32768);
		*buffer++ = output;
		*buffer++ = output;
	}

	return length;
}

void PrintBufferSize(HANDLE hOut, DWORD buflen)
{
	COORD pos = { 1, 8 };
	PrintConsole(hOut, pos, " -   +  Buffer: %3dms", buflen);

	DWORD written;
	FillConsoleOutputAttribute(hOut, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | BACKGROUND_RED, 3, pos, &written);
	pos.X += 4;
	FillConsoleOutputAttribute(hOut, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | BACKGROUND_BLUE, 3, pos, &written);
}

void PrintOutputScale(HANDLE hOut)
{
	COORD pos = { 1, 9 };
	PrintConsole(hOut, pos, "F11 F12 Output: %5.1f%%", output_scale * 100.0f);

	DWORD written;
	FillConsoleOutputAttribute(hOut, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | BACKGROUND_RED, 3, pos, &written);
	pos.X += 4;
	FillConsoleOutputAttribute(hOut, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | BACKGROUND_BLUE, 3, pos, &written);
}

void PrintKeyOctave(HANDLE hOut)
{
	COORD pos = { 1, 10 };
	PrintConsole(hOut, pos, " [   ]  Key Octave: %d", keyboard_octave);

	DWORD written;
	FillConsoleOutputAttribute(hOut, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | BACKGROUND_RED, 3, pos, &written);
	pos.X += 4;
	FillConsoleOutputAttribute(hOut, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | BACKGROUND_BLUE, 3, pos, &written);
}

void MenuLFO(HANDLE hOut, WORD key, DWORD modifiers)
{
	COORD pos = { 1, 12 };
	DWORD written;
	int sign;

	switch (key)
	{
	case VK_UP:
		if (--menu_item[MENU_LFO] < 0)
			menu_item[MENU_LFO] = 2;
		break;

	case VK_DOWN:
		if (++menu_item[MENU_LFO] > 2)
			menu_item[MENU_LFO] = 0;
		break;

	case VK_LEFT:
	case VK_RIGHT:
		sign = (key == VK_RIGHT) ? 1 : -1;
		switch (menu_item[MENU_LFO])
		{
		case 0:
			lfo_config.wavetype = Wave(lfo_config.wavetype + WAVE_COUNT + sign);
			lfo_state.phase = 0.0f;
			lfo_state.loops = 0;
			break;
		case 1:
			if (modifiers & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
				lfo_config.waveparam += sign * 16.0f / 256.0f;
			else
				lfo_config.waveparam += sign * 1.0f / 256.0f;
			lfo_config.waveparam = Clamp(lfo_config.waveparam, 0.0f, 1.0f);
			break;
		case 2:
			if (key == VK_RIGHT)
				lfo_config.frequency *= 2.0f;
			else
				lfo_config.frequency *= 0.5f;
			break;
		}
		break;
	}

	FillConsoleOutputAttribute(hOut, menu_title_attrib[menu_active == MENU_LFO], 18, pos, &written);

	for (int i = 0; i < 3; ++i)
	{
		COORD p = { pos.X, pos.Y + 1 + i };
		FillConsoleOutputAttribute(hOut, menu_item_attrib[menu_active == MENU_LFO && menu_item[MENU_LFO] == i], 18, p, &written);
	}

	PrintConsole(hOut, pos, "F%d | LFO", MENU_LFO + 1);

	++pos.Y;
	PrintConsole(hOut, pos, "%-10s", wave_name[lfo_config.wavetype]);

	++pos.Y;
	PrintConsole(hOut, pos, "Width: %5.1f%%", lfo_config.waveparam * 100.0f);

	++pos.Y;
	PrintConsole(hOut, pos, "Freq:   %4gHz", lfo_config.frequency);
}

void MenuOSC(HANDLE hOut, WORD key, DWORD modifiers)
{
	COORD pos = { 21, 12 };
	DWORD written;
	int sign;

	switch (key)
	{
	case VK_UP:
		if (--menu_item[MENU_OSC] < 0)
			menu_item[MENU_OSC] = 6;
		break;

	case VK_DOWN:
		if (++menu_item[MENU_OSC] > 6)
			menu_item[MENU_OSC] = 0;
		break;

	case VK_LEFT:
	case VK_RIGHT:
		sign = (key == VK_RIGHT) ? 1 : -1;
		switch (menu_item[MENU_OSC])
		{
		case 0:
			osc_config.wavetype = Wave((osc_config.wavetype + WAVE_COUNT + sign) % WAVE_COUNT);
			for (int k = 0; k < KEYS; ++k)
			{
				osc_state[k].phase = 0.0f;
				osc_state[k].loops = 0;
			}
			break;
		case 1:
			if (modifiers & (SHIFT_PRESSED))
				osc_config.waveparam_base += sign * 256.0f / 256.0f;
			else if (modifiers & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
				osc_config.waveparam_base += sign * 16.0f / 256.0f;
			else
				osc_config.waveparam_base += sign * 1.0f / 256.0f;
			osc_config.waveparam_base = Clamp(osc_config.waveparam_base, 0.0f, 1.0f);
			break;
		case 2:
			if (modifiers & (SHIFT_PRESSED))
				osc_config.frequency_base += sign * 1200.0f / 1200.0f;
			else if (modifiers & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
				osc_config.frequency_base += sign * 100.0f / 1200.0f;
			else if (!(modifiers & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)))
				osc_config.frequency_base += sign * 10.0f / 1200.0f;
			else
				osc_config.frequency_base += sign * 1.0f / 1200.0f;
			break;
		case 3:
			if (modifiers & (SHIFT_PRESSED))
				osc_config.amplitude_base += sign * 256.0f / 256.0f;
			else if (modifiers & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
				osc_config.amplitude_base += sign * 16.0f / 256.0f;
			else
				osc_config.amplitude_base += sign * 1.0f / 256.0f;
			break;
		case 4:
			if (modifiers & (SHIFT_PRESSED))
				osc_config.waveparam_lfo += sign * 256.0f / 256.0f;
			else if (modifiers & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
				osc_config.waveparam_lfo += sign * 16.0f / 256.0f;
			else
				osc_config.waveparam_lfo += sign * 1.0f / 256.0f;
			break;
		case 5:
			if (modifiers & (SHIFT_PRESSED))
				osc_config.frequency_lfo += sign * 100.0f / 1200.0f;
			else if (modifiers & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
				osc_config.frequency_lfo += sign * 10.0f / 1200.0f;
			else
				osc_config.frequency_lfo += sign * 1.0f / 1200.0f;
			break;
		case 6:
			if (modifiers & (SHIFT_PRESSED))
				osc_config.amplitude_lfo += sign * 100.0f / 1200.0f;
			else if (modifiers & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
				osc_config.amplitude_lfo += sign * 16.0f / 256.0f;
			else
				osc_config.amplitude_lfo += sign * 1.0f / 256.0f;
			break;
		}
		break;
	}

	FillConsoleOutputAttribute(hOut, menu_title_attrib[menu_active == MENU_OSC], 18, pos, &written);

	for (int i = 0; i < 7; ++i)
	{
		COORD p = { pos.X, pos.Y + 1 + i };
		FillConsoleOutputAttribute(hOut, menu_item_attrib[menu_active == MENU_OSC && menu_item[MENU_OSC] == i], 18, p, &written);
	}

	PrintConsole(hOut, pos, "F%d | OSC", MENU_OSC + 1);

	++pos.Y;
	PrintConsole(hOut, pos, "%-10s", wave_name[osc_config.wavetype]);

	++pos.Y;
	PrintConsole(hOut, pos, "Parm Base: % 6.1f%%", osc_config.waveparam_base * 100.0f);

	++pos.Y;
	PrintConsole(hOut, pos, "Freq Base:  % 6.2f", roundf(osc_config.frequency_base * 1200.0f) / 100.0f);

	++pos.Y;
	PrintConsole(hOut, pos, "Amp Base:  % 6.1f%%", osc_config.amplitude_base * 100.0f);

	++pos.Y;
	PrintConsole(hOut, pos, "Parm LFO:  % 6.1f%%", osc_config.waveparam_lfo * 100.0f);

	++pos.Y;
	PrintConsole(hOut, pos, "Freq LFO:   % 6.2f", roundf(osc_config.frequency_lfo * 1200.0f) / 100.0f);

	++pos.Y;
	PrintConsole(hOut, pos, "Amp LFO:   % 6.1f%%", osc_config.amplitude_lfo * 100.0f);
}

void MenuFLT(HANDLE hOut, WORD key, DWORD modifiers)
{
	COORD pos = { 41, 12 };
	DWORD written;
	int sign;

	switch (key)
	{
	case VK_UP:
		if (--menu_item[MENU_FLT] < 0)
			menu_item[MENU_FLT] = 8;
		break;

	case VK_DOWN:
		if (++menu_item[MENU_FLT] > 8)
			menu_item[MENU_FLT] = 0;
		break;

	case VK_LEFT:
	case VK_RIGHT:
		sign = (key == VK_RIGHT) ? 1 : -1;
		switch (menu_item[MENU_FLT])
		{
		case 0:
			flt_config.mode = FilterConfig::Mode((flt_config.mode + FilterConfig::COUNT + sign) % FilterConfig::COUNT);
			break;
		case 1:
			if (modifiers & (SHIFT_PRESSED))
				flt_config.resonance += sign * 256.0f / 256.0f;
			else if (modifiers & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
				flt_config.resonance += sign * 16.0f / 256.0f;
			else
				flt_config.resonance += sign * 1.0f / 256.0f;
			flt_config.resonance = Clamp(flt_config.resonance, 0.0f, 4.0f);
			break;
		case 2:
			if (modifiers & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
				flt_config.cutoff += sign * 12.0f;
			else
				flt_config.cutoff += sign * 1.0f;
			break;
		case 3:
			if (modifiers & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
				flt_config.cutoff_lfo += sign * 12.0f;
			else
				flt_config.cutoff_lfo += sign * 1.0f;
			break;
		case 4:
			if (modifiers & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
				flt_config.cutoff_env += sign * 12.0f;
			else
				flt_config.cutoff_env += sign * 1.0f;
			break;
		case 5:
			if (key == VK_RIGHT)
				flt_env_config.attack_rate *= 2.0f;
			else
				flt_env_config.attack_rate *= 0.5f;
			break;
		case 6:
			if (key == VK_RIGHT)
				flt_env_config.decay_rate *= 2.0f;
			else
				flt_env_config.decay_rate *= 0.5f;
			break;
		case 7:
			if (modifiers & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
				flt_env_config.sustain_level += sign * 16.0f / 256.0f;
			else
				flt_env_config.sustain_level += sign * 1.0f / 256.0f;
			flt_env_config.sustain_level = Clamp(flt_env_config.sustain_level, 0.0f, 1.0f);
			break;
		case 8:
			if (key == VK_RIGHT)
				flt_env_config.release_rate *= 2.0f;
			else
				flt_env_config.release_rate *= 0.5f;
			break;
		}
		break;
	}

	FillConsoleOutputAttribute(hOut, menu_title_attrib[menu_active == MENU_FLT], 18, pos, &written);

	for (int i = 0; i < 9; ++i)
	{
		COORD p = { pos.X, pos.Y + 1 + i };
		FillConsoleOutputAttribute(hOut, menu_item_attrib[menu_active == MENU_FLT && menu_item[MENU_FLT] == i], 18, p, &written);
	}

	PrintConsole(hOut, pos, "F%d | FLT", MENU_FLT + 1);

	++pos.Y;
	PrintConsole(hOut, pos, "%-18s", filter_name[flt_config.mode]);

	++pos.Y;
	PrintConsole(hOut, pos, "Resonance:  %5.3f", flt_config.resonance);

	++pos.Y;
	PrintConsole(hOut, pos, "Cutoff Base: %5.0f", flt_config.cutoff);

	++pos.Y;
	PrintConsole(hOut, pos, "Cutoff LFO:  %5.0f", flt_config.cutoff_lfo);

	++pos.Y;
	PrintConsole(hOut, pos, "Cutoff ENV:  %5.0f", flt_config.cutoff_env);

	++pos.Y;
	PrintConsole(hOut, pos, "Attack:  %5g", flt_env_config.attack_rate);

	++pos.Y;
	PrintConsole(hOut, pos, "Decay:   %5g", flt_env_config.decay_rate);

	++pos.Y;
	PrintConsole(hOut, pos, "Sustain: %5.1f%%", flt_env_config.sustain_level * 100.0f);

	++pos.Y;
	PrintConsole(hOut, pos, "Release: %5g", flt_env_config.release_rate);
}

void MenuVOL(HANDLE hOut, WORD key, DWORD modifiers)
{
	COORD pos = { 61, 12 };
	DWORD written;
	int sign;

	switch (key)
	{
	case VK_UP:
		if (--menu_item[MENU_VOL] < 0)
			menu_item[MENU_VOL] = 3;
		break;

	case VK_DOWN:
		if (++menu_item[MENU_VOL] > 3)
			menu_item[MENU_VOL] = 0;
		break;

	case VK_LEFT:
	case VK_RIGHT:
		sign = (key == VK_RIGHT) ? 1 : -1;
		switch (menu_item[MENU_VOL])
		{
		case 0:
			if (key == VK_RIGHT)
				vol_env_config.attack_rate *= 2.0f;
			else
				vol_env_config.attack_rate *= 0.5f;
			break;
		case 1:
			if (key == VK_RIGHT)
				vol_env_config.decay_rate *= 2.0f;
			else
				vol_env_config.decay_rate *= 0.5f;
			break;
		case 2:
			if (modifiers & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
				vol_env_config.sustain_level += sign * 16.0f / 256.0f;
			else
				vol_env_config.sustain_level += sign * 1.0f / 256.0f;
			vol_env_config.sustain_level = Clamp(vol_env_config.sustain_level, 0.0f, 1.0f);
			break;
		case 3:
			if (key == VK_RIGHT)
				vol_env_config.release_rate *= 2.0f;
			else
				vol_env_config.release_rate *= 0.5f;
			break;
		}
		break;
	}

	FillConsoleOutputAttribute(hOut, menu_title_attrib[menu_active == MENU_VOL], 18, pos, &written);

	for (int i = 0; i < 4; ++i)
	{
		COORD p = { pos.X, pos.Y + 1 + i };
		FillConsoleOutputAttribute(hOut, menu_item_attrib[menu_active == MENU_VOL && menu_item[MENU_VOL] == i], 18, p, &written);
	}

	PrintConsole(hOut, pos, "F%d | VOL", MENU_VOL + 1);

	++pos.Y;
	PrintConsole(hOut, pos, "Attack:  %5g", vol_env_config.attack_rate);

	++pos.Y;
	PrintConsole(hOut, pos, "Decay:   %5g", vol_env_config.decay_rate);

	++pos.Y;
	PrintConsole(hOut, pos, "Sustain: %5.1f%%", vol_env_config.sustain_level * 100.0f);

	++pos.Y;
	PrintConsole(hOut, pos, "Release: %5g", vol_env_config.release_rate);
}

static void EnableEffect(int index)
{
	if (!fx[index])
	{
		// set the effect, not bothering with parameters (use defaults)
		fx[index] = BASS_ChannelSetFX(stream, BASS_FX_DX8_CHORUS + index, 0);
	}
}

static void DisableEffect(int index)
{
	if (fx[index])
	{
		BASS_ChannelRemoveFX(stream, fx[index]);
		fx[index] = 0;
	}
}

void MenuFX(HANDLE hOut, WORD key, DWORD modifiers)
{
	char buf[64];
	COORD pos = { 61, 1 };
	DWORD written;

	switch (key)
	{
	case VK_UP:
		if (--menu_item[MENU_FX] < 0)
			menu_item[MENU_FX] = ARRAY_SIZE(fx) - 1;
		break;

	case VK_DOWN:
		if (++menu_item[MENU_FX] > ARRAY_SIZE(fx) - 1)
			menu_item[MENU_FX] = 0;
		break;

	case VK_LEFT:
		DisableEffect(menu_item[MENU_FX]);
		break;

	case VK_RIGHT:
		EnableEffect(menu_item[MENU_FX]);
		break;
	}

	PrintConsole(hOut, pos, "F5 | FX");
	FillConsoleOutputAttribute(hOut, menu_title_attrib[menu_active == MENU_FX], 18, pos, &written);

	for (int i = 0; i < ARRAY_SIZE(fx); ++i)
	{
		++pos.Y;
		PrintConsole(hOut, pos, "%-11s %-3s", fx_name[i], fx[i] ? "ON" : "off");
		FillConsoleOutputAttribute(hOut, menu_item_attrib[menu_active == MENU_FX && menu_item[MENU_FX] == i], 18, pos, &written);
	}
}

typedef void(*MenuFunc)(HANDLE hOut, WORD key, DWORD modifiers);
MenuFunc menufunc[MENU_COUNT] =
{
	MenuLFO, MenuOSC, MenuFLT, MenuVOL, MenuFX
};

void Clear(HANDLE hOut)
{
	CONSOLE_SCREEN_BUFFER_INFO bufInfo;
	COORD osc_phase = { 0, 0 };
	DWORD written;
	DWORD size;
	GetConsoleScreenBufferInfo(hOut, &bufInfo);
	size = bufInfo.dwSize.X * bufInfo.dwSize.Y;
	FillConsoleOutputCharacter(hOut, (TCHAR)' ', size, osc_phase, &written);
	FillConsoleOutputAttribute(hOut, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE, size, osc_phase, &written);
	SetConsoleCursorPosition(hOut, osc_phase);
}

void main(int argc, char **argv)
{
	INPUT_RECORD keyin;
	DWORD r, buflen;
	int k;
	HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
	HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	int running = 1;

#ifdef WIN32
	if (IsDebuggerPresent())
	{
		// turn on floating-point exceptions
		unsigned int prev;
		_controlfp_s(&prev, 0, _EM_ZERODIVIDE|_EM_INVALID);
	}
#endif

	// check the correct BASS was loaded
	if (HIWORD(BASS_GetVersion()) != BASSVERSION)
	{
		fprintf(stderr, "An incorrect version of BASS.DLL was loaded");
		return;
	}

	// set the window title
	SetConsoleTitle(TEXT(title_text));

#if 0
	// set the console buffer size
	static const COORD bufferSize = { 80, 50 };
	SetConsoleScreenBufferSize(hOut, bufferSize);

	// set the console window size
	static const SMALL_RECT windowSize = { 0, 0, 79, 49 };
	SetConsoleWindowInfo(hOut, TRUE, &windowSize);
#endif

	// clear the window
	Clear(hOut);

	// hide the cursor
	static const CONSOLE_CURSOR_INFO cursorInfo = { 100, FALSE };
	SetConsoleCursorInfo(hOut, &cursorInfo);

	// set input mode
	SetConsoleMode(hIn, 0);

	// 10ms update period
	BASS_SetConfig(BASS_CONFIG_UPDATEPERIOD, 10);

	// setup output - get latency
	//if (!BASS_Init(-1, 44100, BASS_DEVICE_LATENCY, 0, NULL))
	if (!BASS_Init(-1, 48000, BASS_DEVICE_LATENCY, 0, NULL))
		Error("Can't initialize device");

	BASS_GetInfo(&info);
	// default buffer size = update period + 'minbuf' + 1ms extra margin
	BASS_SetConfig(BASS_CONFIG_BUFFER, 10 + info.minbuf + 1);
	buflen = BASS_GetConfig(BASS_CONFIG_BUFFER);
	// if the device's output rate is unknown default to 44100 Hz
	if (!info.freq) info.freq = 44100;
	// create a stream, stereo so that effects sound nice
	stream = BASS_StreamCreate(info.freq, 2, 0, (STREAMPROC*)WriteStream, 0);

	// initialize polynomial noise tables
	InitPoly(poly4, 4, 1, 0xF, 0);
	InitPoly(poly5, 5, 2, 0x1F, 1);
	InitPoly(poly17, 17, 5, 0x1FFFF, 0);

	// compute phase advance per sample per key
	for (k = 0; k < KEYS; ++k)
	{
		keyboard_frequency[k] = pow(2.0, (k + 3) / 12.0) * 220.0;	// middle C = 261.626Hz; A above middle C = 440Hz
		//keyboard_frequency[k] = pow(2.0, k / 12.0) * 256.0f;		// middle C = 256Hz
	}

	DebugPrint("device latency: %dms\n", info.latency);
	DebugPrint("device minbuf: %dms\n", info.minbuf);
	DebugPrint("ds version: %d (effects %s)\n", info.dsver, info.dsver < 8 ? "disabled" : "enabled");

	// show the note keys
	for (k = 0; k < KEYS; ++k)
	{
		char buf[] = { keys[k], '\0' };
		COORD pos = { key_pos.X + k, key_pos.Y };
		WORD attrib = FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED;
		DWORD written;
		WriteConsoleOutputAttribute(hOut, &attrib, 1, pos, &written);
		WriteConsoleOutputCharacter(hOut, buf, 1, pos, &written);
	}

	PrintBufferSize(hOut, buflen);
	PrintOutputScale(hOut);
	PrintKeyOctave(hOut);

	// initialize all menus
	for (int i = 0; i < MENU_COUNT - (info.dsver < 8); ++i)
		menufunc[i](hOut, 0, 0);

	/*
	printf(
		"press -/+ to de/increase the buffer\n"
		"press [/] to de/increase the octave\n"
		"press ,/. to switch the waveform\n"
		"press escape to quit\n\n");
	if (info.dsver >= 8) // DX8 effects available
		printf("press F1-F9 to toggle effects\n\n");
	printf("using a %dms buffer\r", buflen);
	*/

	BASS_ChannelPlay(stream, FALSE);

	EnvelopeState::State vol_env_display[KEYS] = { EnvelopeState::OFF };

	while (running)
	{
		// if there are any pending input events
		DWORD numEvents = 0;
		while (GetNumberOfConsoleInputEvents(hIn, &numEvents) && numEvents > 0)
		{
			INPUT_RECORD keyin;
			ReadConsoleInput(hIn, &keyin, 1, &numEvents);
			if (keyin.EventType == KEY_EVENT)
			{
				if (keyin.Event.KeyEvent.bKeyDown)
				{
					WORD code = keyin.Event.KeyEvent.wVirtualKeyCode;
					DWORD modifiers = keyin.Event.KeyEvent.dwControlKeyState;
					if (code == VK_ESCAPE)
					{
						running = 0;
						break;
					}
					else if (code == VK_F11)
					{
						output_scale -= 1.0f / 16.0f;
						PrintOutputScale(hOut);
					}
					else if (code == VK_F12)
					{
						output_scale += 1.0f / 16.0f;
						PrintOutputScale(hOut);
					}
					else if (code == VK_SUBTRACT || code == VK_ADD)
					{
						// recreate stream with smaller/larger buffer
						BASS_StreamFree(stream);
						if (code == VK_SUBTRACT)
							BASS_SetConfig(BASS_CONFIG_BUFFER, buflen - 1); // smaller buffer
						else
							BASS_SetConfig(BASS_CONFIG_BUFFER, buflen + 1); // larger buffer
						buflen = BASS_GetConfig(BASS_CONFIG_BUFFER);
						PrintBufferSize(hOut, buflen);
						stream = BASS_StreamCreate(info.freq, 2, 0, (STREAMPROC*)WriteStream, 0);
						// set effects on the new stream
						for (r = 0; r < 9; r++) if (fx[r]) fx[r] = BASS_ChannelSetFX(stream, BASS_FX_DX8_CHORUS + r, 0);
						BASS_ChannelPlay(stream, FALSE);
					}
					else if (code == VK_OEM_4)	// '['
					{
						if (keyboard_octave > 1)
						{
							--keyboard_octave;
							keyboard_timescale *= 0.5f;
							PrintKeyOctave(hOut);
						}
					}
					else if (code == VK_OEM_6)	// ']'
					{
						if (keyboard_octave < 7)
						{
							++keyboard_octave;
							keyboard_timescale *= 2.0f;
							PrintKeyOctave(hOut);
						}
					}
					else if (code >= VK_F1 && code < VK_F1 + MENU_COUNT - (info.dsver < 8))
					{
						int prev_active = menu_active;
						menu_active = MENU_COUNT;
						menufunc[prev_active](hOut, 0, 0);
						menu_active = MenuMode(code - VK_F1);
						menufunc[menu_active](hOut, 0, 0);
					}
					else if (code == VK_TAB)
					{
						int prev_active = menu_active;
						menu_active = MENU_COUNT;
						menufunc[prev_active](hOut, 0, 0);
						if (modifiers & SHIFT_PRESSED)
							menu_active = MenuMode(prev_active == 0 ? MENU_COUNT - 1 : prev_active - 1);
						else
							menu_active = MenuMode(prev_active == MENU_COUNT - 1 ? 0 : prev_active + 1);
						menufunc[menu_active](hOut, 0, 0);
					}
					else if (code == VK_UP || code == VK_DOWN || code == VK_RIGHT || code == VK_LEFT)
					{
						menufunc[menu_active](hOut, code, modifiers);
					}
				}

				for (k = 0; k < KEYS; k++)
				{
					if (keyin.Event.KeyEvent.wVirtualKeyCode == keys[k])
					{
						if (flt_env_state[k].gate != bool(keyin.Event.KeyEvent.bKeyDown))
						{
							flt_env_state[k].gate = bool(keyin.Event.KeyEvent.bKeyDown);

							if (flt_env_state[k].gate)
							{
								flt_env_state[k].state = EnvelopeState::ATTACK;
							}
							else
							{
								flt_env_state[k].state = EnvelopeState::RELEASE;
							}
						}
						if (vol_env_state[k].gate != bool(keyin.Event.KeyEvent.bKeyDown))
						{
							vol_env_state[k].gate = bool(keyin.Event.KeyEvent.bKeyDown);

							if (vol_env_state[k].gate)
							{
								if (vol_env_state[k].state == EnvelopeState::OFF)
								{
									// start the oscillator
									// (assume restart on key)
									osc_state[k].phase = 0;
									osc_state[k].loops = 0;

									// start the filter
									flt_state[k].Clear();
								}

								vol_env_state[k].state = EnvelopeState::ATTACK;
							}
							else
							{
								vol_env_state[k].state = EnvelopeState::RELEASE;
							}
						}
						break;
					}
				}
			}
		}

		// compute spectrum
#define SPECTRUM_WIDTH 60
#define SPECTRUM_HEIGHT 6
		float fft[8192];
		BASS_ChannelGetData(stream, fft, BASS_DATA_FFT16384); // get the FFT data
		// i = f * FFT_SIZE * info.freq
		// f = i * info.freq / FFT_SIZE
		float const step = powf(2, 1.0f / 12.0f);
		float freq = keyboard_frequency[0] * keyboard_timescale * ARRAY_SIZE(fft) * 2.0f / info.freq / sqrtf(step);
		//float const step = powf(2, 2.0f / 12.0f);
		//float freq = 16 * ARRAY_SIZE(fft) * 2.0f / info.freq / sqrtf(step);
		int b0 = int(freq);
		float specbuf[SPECTRUM_WIDTH];
		for (int x = 0; x < SPECTRUM_WIDTH; ++x)
		{
			freq *= step;
			int const b1 = Min(int(freq), int(ARRAY_SIZE(fft) - 1));
			float peak = 0.0f;
			if (b0 == b1)
				--b0;
			float scale = 8192 / (b1 - b0);
			for (; b0 < b1; ++b0)
				peak += fft[b0] * fft[b0];
			specbuf[x] = scale * peak;
		}

		// draw spectrum
		CHAR_INFO buf[SPECTRUM_HEIGHT][SPECTRUM_WIDTH];
		COORD const pos = { 0, 0 };
		COORD const size = { SPECTRUM_WIDTH, SPECTRUM_HEIGHT };
		SMALL_RECT region = { pos.X, pos.Y, pos.X + size.X - 1, pos.Y + size.Y - 1 };
		float threshold = 0.25f;
		CHAR_INFO const bar_full = { 0, BACKGROUND_RED };
		CHAR_INFO const bar_top = { 223, BACKGROUND_RED | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE };
		CHAR_INFO const bar_bottom = { 220, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE };
		CHAR_INFO const bar_empty = { 0, 0 };
		for (int y = 0; y < SPECTRUM_HEIGHT; ++y)
		{
			for (int x = 0; x < SPECTRUM_WIDTH; ++x)
			{
				if (specbuf[x] < threshold)
					buf[y][x] = bar_empty;
				else if (specbuf[x] < threshold * 2.0f)
					buf[y][x] = bar_bottom;
				else if (specbuf[x] < threshold * 4.0f)
					buf[y][x] = bar_top;
				else
					buf[y][x] = bar_full;
			}
			threshold *= 0.25f;
		}
		WriteConsoleOutput(hOut, &buf[0][0], size, pos, &region);

		// update volume envelope display
		for (k = 0; k < KEYS; k++)
		{
			if (vol_env_display[k] != vol_env_state[k].state)
			{
				vol_env_display[k] = vol_env_state[k].state;
				COORD pos = { key_pos.X + k, key_pos.Y };
				DWORD written;
				WriteConsoleOutputAttribute(hOut, &env_attrib[vol_env_display[k]], 1, pos, &written);
			}
		}
		
		Sleep(50);
	}

	// clear the window
	Clear(hOut);

	BASS_Free();
}
