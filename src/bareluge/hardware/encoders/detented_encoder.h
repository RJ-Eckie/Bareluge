#pragma once
#include "hardware/encoders/encoder.h"

namespace bareluge {

class DetentedEncoder : public Encoder {
public:
	// Constructor
	DetentedEncoder(uint8_t index) : Encoder(index) {}

	// Process values gathered by readRotation()
	bool processRotation(bool& clockwise) override {
		// No change recorded by readRotation
		if (!Encoder::processRotation(clockwise)) {
			// Return not rotated
			return false;
		}
		// Rotation smaller than one detent
		if (std::abs(deltaRotation) < DetentSize) {
			// Return not rotated
			return false;
		}
		// Reset deltaRotation
		deltaRotation -= clockwise ? DetentSize : -DetentSize;
		// Return rotated
		return true;
	}

private:
	// Constant
	static constexpr uint8_t DetentSize = 4;
};

} // namespace bareluge
