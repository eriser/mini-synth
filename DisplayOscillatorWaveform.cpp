/*
MINI VIRTUAL ANALOG SYNTHESIZER
Copyright 2014 Kenneth D. Miller III

Oscillator Waveform Display
*/
#include "StdAfx.h"

#include "DisplayOscillatorWaveform.h"
#include "Math.h"
#include "OscillatorLFO.h"
#include "OscillatorNote.h"
#include "SubOscillator.h"
#include "Filter.h"
#include "Envelope.h"
#include "Wave.h"
#include "Keys.h"

#define WAVEFORM_WIDTH 80
#define WAVEFORM_HEIGHT 20
#define WAVEFORM_MIDLINE 10

// show the oscillator wave shape
// (using the lowest key frequency as reference)
static WORD const positive = BACKGROUND_BLUE, negative = BACKGROUND_RED;
static CHAR_INFO const plot[2] = {
	{ 223, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE },
	{ 220, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE }
};
static COORD const pos = { 0, 0 };
static COORD const size = { WAVEFORM_WIDTH, WAVEFORM_HEIGHT };

// waveform display settings
void UpdateOscillatorWaveformDisplay(HANDLE hOut, BASS_INFO const &info, int const k)
{
	// display region
	SMALL_RECT region = { 0, 50 - WAVEFORM_HEIGHT, WAVEFORM_WIDTH - 1, 49 };

	// waveform buffer
	CHAR_INFO buf[WAVEFORM_HEIGHT][WAVEFORM_WIDTH] = { 0 };

	// get low-frequency oscillator value
	// (assume it is constant for the duration)
	float const lfo = lfo_state.Update(lfo_config, 1.0f, 0.0f);

	// how many cycles to plot?
	int cycle = wave_loop_cycle[osc_config[0].wavetype];
	if (cycle == INT_MAX)
	{
		if (osc_config[0].sub_osc_mode == SUBOSC_NONE)
			cycle = 1;
		else if (osc_config[0].sub_osc_mode < SUBOSC_SQUARE_2OCT)
			cycle = 2;
		else
			cycle = 4;
	}
	else if (cycle > WAVEFORM_WIDTH)
	{
		cycle = WAVEFORM_WIDTH;
	}

	// base phase step for plot
	float const step_base = cycle / float(WAVEFORM_WIDTH);

	// key frequency
	float const key_freq = keyboard_frequency[k] * keyboard_timescale;

	// update filter envelope generator
	float const flt_env_amplitude = flt_env_state[k].amplitude;

	// update volume envelope generator
	float const vol_env_amplitude = vol_env_config.enable ? vol_env_state[k].amplitude : 1;

	// base phase delta
	float const delta_base = key_freq / info.freq;

	// local oscillators for plot
	NoteOscillatorConfig config[NUM_OSCILLATORS];
	OscillatorState state[NUM_OSCILLATORS];
	float step[NUM_OSCILLATORS];
	float delta[NUM_OSCILLATORS];

	for (int o = 0; o < NUM_OSCILLATORS; ++o)
	{
		// copy current oscillator config
		config[o] = osc_config[o];

		// compute shared oscillator values
		// (assume that LFO doesn't change over the plotted period)
		config[o].Modulate(lfo);

		// step and delta phase
		float const relative = config[o].frequency / config[0].frequency;
		step[o] = step_base * relative;
		delta[o] = delta_base * config[0].frequency * relative;

		// half-step initial phase
		state[o].phase = 0.5f * step[o];
	}

	// local filter for plot
	static FilterState filter;

	// detect when to reset the filter
	static bool prev_active;
	static int prev_k;

	// compute cutoff frequency
	// (assume key follow)
	float const cutoff = powf(2, flt_config.cutoff_base + flt_config.cutoff_lfo * lfo + flt_config.cutoff_env * flt_env_amplitude);

	// elapsed time in milliseconds since the previous frame
	static DWORD prevTime = timeGetTime();
	DWORD deltaTime = timeGetTime() - prevTime;
	prevTime += deltaTime;

	// reset filter state on playing a note
	if (prev_k != k)
	{
		filter.Reset();
		prev_k = k;
	}
	if (prev_active != (vol_env_state[k].state != EnvelopeState::OFF))
	{
		filter.Reset();
		prev_active = (vol_env_state[k].state != EnvelopeState::OFF);
	}

	// if the filter is enabled...
	if (flt_config.enable)
	{
		// get steps needed to advance OSC1 by the time step
		// (subtracting the steps that the plot itself will take)
		int steps = FloorInt(key_freq * osc_config[0].frequency * deltaTime / 1000 - 1) * WAVEFORM_WIDTH;
		if (steps > 0)
		{
			// "rewind" oscillators so they'll end at zero phase
			for (int o = 0; o < NUM_OSCILLATORS; ++o)
			{
				if (!config[o].enable)
					continue;
				state[o].Advance(config[o], step[config[o].sync_enable ? 0 : o] * steps);
			}

			// run the filter ahead for next time
			for (int x = 0; x < steps; ++x)
			{
				// sum the oscillator outputs
				float value = 0.0f;
				for (int o = 0; o < NUM_OSCILLATORS; ++o)
				{
					if (!config[o].enable)
						continue;

					if (config[o].sync_enable)
						config[o].sync_phase = config[o].frequency / config[0].frequency;
					if (config[o].sub_osc_mode)
						value += config[o].sub_osc_amplitude * SubOscillator(config[o], state[o], 1, delta[o]);
					value += state[o].Compute(config[o], delta[o]);
					state[o].Advance(config[o], step[o]);
				}

				// apply filter
				value = filter.Update(flt_config, cutoff, value, step_base);

				// apply volume envelope
				value *= vol_env_amplitude;
			}
		}
	}

#ifdef SCROLL_WAVEFORM_DISPLAY
	// scroll through the waveform
	static float phase_offset = 0.0f;
	static float index_offset = 0;
	phase_offset += 0.3f * step;
	if (phase_offset >= 1.0f)
	{
		phase_offset -= 1.0f;
		if (++index_offset >= wave_loop_cycle[config.wavetype])
			index_offset = 0;
	}
	state.phase += phase_offset;
	state.index += index_offset;
#endif

	for (int x = 0; x < WAVEFORM_WIDTH; ++x)
	{
		// sum the oscillator outputs
		float value = 0.0f;
		for (int o = 0; o < NUM_OSCILLATORS; ++o)
		{
			if (!config[o].enable)
				continue;
			if (config[o].sync_enable)
				config[o].sync_phase = config[o].frequency / config[0].frequency;
			if (config[o].sub_osc_mode)
				value += config[o].sub_osc_amplitude * SubOscillator(config[o], state[o], 1, delta[o]);
			value += state[o].Compute(config[o], delta[o]);
			state[o].Advance(config[o], step[o]);
		}

		if (flt_config.enable)
		{
			// apply filter
			value = filter.Update(flt_config, cutoff, value, step_base);
		}

		// apply volume envelope
		value *= vol_env_amplitude;

		// plot waveform column
		int grid_y = FloorInt(-(WAVEFORM_HEIGHT - 0.5f) * value);
		int y = WAVEFORM_MIDLINE + (grid_y >> 1);
		if (value > 0.0f)
		{
			if (y >= 0)
			{
				buf[y][x] = plot[grid_y & 1];
				y += grid_y & 1;
			}
			else
			{
				y = 0;
			}

			for (int fill = y; fill < WAVEFORM_MIDLINE; ++fill)
				buf[fill][x].Attributes |= positive;
		}
		else
		{
			if (y < WAVEFORM_HEIGHT)
			{
				buf[y][x] = plot[grid_y & 1];
				--y;
				y += grid_y & 1;
			}
			else
			{
				y = WAVEFORM_HEIGHT - 1;
			}
			for (int fill = y; fill >= WAVEFORM_MIDLINE; --fill)
				buf[fill][x].Attributes |= negative;
		}
	}
	WriteConsoleOutput(hOut, &buf[0][0], size, pos, &region);
}
