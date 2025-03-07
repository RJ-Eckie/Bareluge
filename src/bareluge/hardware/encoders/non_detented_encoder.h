#pragma once
#include "hardware/encoders/encoder.h"

namespace bareluge {

class NonDetentedEncoder : public Encoder {
public:
	// Constructor
	NonDetentedEncoder(uint8_t index) : Encoder(index) {}

	// Process values gathered by readRotation()
	bool processRotation(bool& clockwise) override {
		// No change recorded by readRotation
		if (!Encoder::processRotation(clockwise)) {
			// Return not rotated
			return false;
		}
		// == Debouncing ==
		//
		// 1. A bounce is defined by two successive rotations of the encoder adding up to zero. This is interpreted as
		// an accidental wiggle, with the encoder ending up in the same position it started from. Wiggles can be
		// chained, so an input of (-3), (+3), (-3) will be registered as two successive wiggles, and there will be no
		// rotation returned to the ui
		// 2. If two successive rotations that add up to zero happen with some time (DebounceTime or more) between them,
		// this is not registered as an accidental wiggle. To check for a wiggle after DebounceTime or more have passed,
		// we need two frames of input to compare. For this reason, the first input after DebounceTime or more is logged
		// for comparison, but not returned to the ui as a rotation

		// If the last two rotations add up to zero (this is a wiggle)
		if ((deltaRotation + debounceDeltaRotation == 0)
		    // or DebounceTime has passed since last rotation (we need two frames for comparison)
		    || TK::hasPassed(DebounceTime, lastRotated)) {
			// Log this frame's rotation, we don't return any rotation to the ui this frame
			debounceDeltaRotation = deltaRotation;
		}
		// Valid rotation
		else {
			// No longer checking for bounces
			debounceDeltaRotation = 0;
		}
		// Save last rotation time
		TK::log(lastRotated);
		// In all situations deltaRotation is processed, so we reset it to zero
		deltaRotation = 0;
		// If we're checking for a bounce
		if (debounceDeltaRotation) {
			// Return not rotated
			return false;
		}
		// Return rotated
		return true;
	}

private:
	// Constant
	static constexpr uint16_t DebounceTime = TK::toSamples(500);

	// Properties
	int8_t debounceDeltaRotation{}; // Remember delta rotation for debounce checking
	uint32_t lastRotated{};         // Last time encoder was rotated
};

} // namespace bareluge
