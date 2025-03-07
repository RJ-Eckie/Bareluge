#pragma once
#include "hardware/handlers/pic_handler.h"

namespace bareluge {

// Vertical led bar to the left of a gold encoder

class LedBar {
public:
	// Constant
	static constexpr uint8_t NumSquares = 4;

	// Constructor
	LedBar(PicHandler& picHandler, bool isTopLedBar) : picHandler(picHandler), isTopLedBar(isTopLedBar) {}

	// Actions

	void clear() {
		brightness.fill(0);
		updateLeds();
	}

	void setSquareBrightness(uint8_t squareId, uint8_t squareBrightness) {
		this->brightness[squareId % NumSquares] = squareBrightness;
		updateLeds();
	}

	void setBrightness(std::array<uint8_t, NumSquares> brightness) {
		this->brightness = brightness;
		updateLeds();
	}

	// Range 0 - 255
	void setValue(uint8_t value, bool isUnipolar = true) {
		for (uint8_t i = 0; i < NumSquares; i++) {
			// Unipolar
			if (isUnipolar) {
				// Exp curve gives a more natural gradient
				brightness[i] = exp_curve(static_cast<uint8_t>(std::clamp(value * NumSquares - i * 256, 0, 255)), 255);
			}
			// Bipolar
			else {
				// Center value
				if (value == 127 || value == 128) {
					// Lights up the center two leds
					brightness[i] = (std::abs(i - (NumSquares - 1) / 2.0) < 1) * 255;
				}
				// Non-center value
				else {
					brightness[i] =
					    // Exp curve gives a more natural gradient
					    exp_curve(static_cast<uint8_t>(std::clamp(
					                  // Scale value from the center
					                  (i < NumSquares / 2 ? 1 : -1) * (128 - value) * NumSquares
					                      // Apply offset mirrored around absolute center: (NumSquares - 1) / 2.0
					                      - static_cast<uint8_t>(std::abs((NumSquares - 1) / 2.0 - i)) * 256,
					                  0, 255)),
					              255);
				}
			}
		}
		updateLeds();
	}

private:
	// Properties
	PicHandler& picHandler;
	bool isTopLedBar;
	std::array<uint8_t, NumSquares> brightness{};

	// Send led data to pic
	void updateLeds() { picHandler.setGoldKnobIndicator(isTopLedBar, brightness); }
};

} // namespace bareluge
