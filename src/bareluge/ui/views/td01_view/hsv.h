#pragma once
#include "gui/colour/rgb.h"
#include "utilities/utilities.h"

namespace bareluge::TD01 {

// Custom HSV type

class HSV {
public:
	// Constant
	static constexpr uint8_t MaxHue = 192;

	// Properties
	uint8_t h;
	uint8_t s;
	uint8_t v;

	// RJ: I took this off the interwebs and adapted it so it looks good to me in the context of Tech Demo 01. Might not
	// actually be an accurate transformation in all contexts
	static constexpr RGB HSVtoRGB(HSV hsvColor) {
		float r, g, b;

		float h = hsvColor.h / (1.0 * MaxHue);
		float s = log_curve(hsvColor.s, 255) / 255.0;
		float v = exp_curve(hsvColor.v, 255) / 255.0;

		int i = floor(h * 6);
		float f = h * 6 - i;
		float p = v * (1 - s);
		float q = v * (1 - f * s);
		float t = v * (1 - (1 - f) * s);

		switch (i % 6) {
		case 0:
			r = v, g = t, b = p;
			break;
		case 1:
			r = q, g = v, b = p;
			break;
		case 2:
			r = p, g = v, b = t;
			break;
		case 3:
			r = p, g = q, b = v;
			break;
		case 4:
			r = t, g = p, b = v;
			break;
		case 5:
			r = v, g = p, b = q;
			break;
		}

		RGB color;
		color.r = r * 255;
		color.g = g * 255;
		color.b = b * 255;

		return color;
	}
};

} // namespace bareluge::TD01
