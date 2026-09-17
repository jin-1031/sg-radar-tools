#pragma once

#include <cstdint>

namespace colormap
{
	struct Color
	{
		float r;
		float g;
		float b;
	};

	enum Colormap
	{
		Magma,
		Inferno,
		Plasma,
		Viridis,
		Cividis,
		Twilight,
		Turbo,
		Berlin,
		Managua,
		Vanimo
	};

	Color map(Colormap colormap, uint8_t value);
	Color map(Colormap colormap, float value);

	Color magma(uint8_t value);
	Color inferno(uint8_t value);
	Color plasma(uint8_t value);
	Color viridis(uint8_t value);
	Color cividis(uint8_t value);
	Color twilight(uint8_t value);
	Color turbo(uint8_t value);
	Color berlin(uint8_t value);
	Color managua(uint8_t value);
	Color vanimo(uint8_t value);

	Color magma(float value);
	Color inferno(float value);
	Color plasma(float value);
	Color viridis(float value);
	Color cividis(float value);
	Color twilight(float value);
	Color turbo(float value);
	Color berlin(float value);
	Color managua(float value);
	Color vanimo(float value);
};
