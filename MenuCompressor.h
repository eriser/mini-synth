#pragma once

#include "Menu.h"

namespace Menu
{
	class Compressor : public Menu
	{
	public:
		enum Item
		{
			TITLE,
			GAIN,
			ATTACK,
			RELEASE,
			THRESHOLD,
			RATIO,
			PREDELAY,
			COUNT
		};

		// constructor
		Compressor(COORD pos, const char *name, int count)
			: Menu(pos, name, count)
		{
		}

	protected:
		virtual void Update(int index, int sign, DWORD modifiers);
		virtual void Print(int index, HANDLE hOut, COORD pos, DWORD flags);
	};

	extern Compressor menu_fx_compressor;
}
