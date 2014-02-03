/*
MINI VIRTUAL ANALOG SYNTHESIZER
Copyright 2014 Kenneth D. Miller III

Oscillator Frequency Display
*/
#include "StdAfx.h"

#include "DisplayOscillatorFrequency.h"
#include "Menu.h"
#include "MenuOSC.h"
#include "Voice.h"
#include "Control.h"
#include "Console.h"
#include "OscillatorNote.h"

// show oscillator frequency
void UpdateOscillatorFrequencyDisplay(HANDLE hOut, int const v, int const o)
{
	// key frequency (taking pitch wheel control into account)
	float const key_freq = Control::pitch_scale * note_frequency[voice_note[v]];

	// get attributes to use
	COORD const pos = { Menu::menu_osc[o].pos.X + 8, Menu::menu_osc[o].pos.Y };
	bool const selected = (Menu::active_page == Menu::PAGE_MAIN && Menu::active_menu == Menu::MAIN_OSC1 + o);
	bool const title_selected = selected && Menu::menu_osc[o].item == 0;
	WORD const title_attrib = Menu::title_attrib[true][selected + title_selected];
	WORD const num_attrib = (title_attrib & 0xF8) | (FOREGROUND_GREEN);
	WORD const unit_attrib = (title_attrib & 0xF8) | (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);

	// current frequency in Hz
	float const freq = key_freq * osc_config[o].frequency;

	if (freq >= 20000.0f)
	{
		// print in kHz
		PrintConsoleWithAttribute(hOut, pos, num_attrib, "%7.2f", freq / 1000.0f);
		COORD const pos2 = { pos.X + 7, pos.Y };
		PrintConsoleWithAttribute(hOut, pos2, unit_attrib, "kHz");
	}
	else
	{
		// print in Hz
		PrintConsoleWithAttribute(hOut, pos, num_attrib, "%8.2f", freq);
		COORD const pos2 = { pos.X + 8, pos.Y };
		PrintConsoleWithAttribute(hOut, pos2, unit_attrib, "Hz");
	}
}
