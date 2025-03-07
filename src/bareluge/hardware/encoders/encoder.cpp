#include "encoder.h"

namespace bareluge {

void Encoder::readRotation() {
	// This reads out the pins of the encoder. Calling this often makes sure all interactions are recorded correctly.
	// If the time between two calls is too long, a step may be skipped or a large rotation to the right might be
	// registered as a small rotation to the left.

	// Read pins
	pinState[2] = readInput(PinList[index][0], PinList[index][1]);
	pinState[3] = readInput(PinList[index][2], PinList[index][3]);

	// If exactly one of the pins has changed
	if ((pinState[2] != pinState[0]) ^ (pinState[3] != pinState[1])) {
		// Work out direction of change
		if (pinState[2] != pinState[0]) {
			deltaRotation += (pinState[2] == pinState[1]) ? 1 : -1;
		}
		else {
			deltaRotation += (pinState[3] == pinState[0]) ? -1 : 1;
		}
	}
	// If both pins changed since last read
	else if (pinState[2] != pinState[0]) {
		D_PRINT("Warning: encoder skipping");
	}
	// Save current state of pins
	pinState[0] = pinState[2];
	pinState[1] = pinState[3];
}

bool Encoder::processRotation(bool& clockwise) {
	// No rotation
	if (deltaRotation == 0) {
		// Return not rotated
		return false;
	}
	// Save in which direction the encoder is being turned
	clockwise = deltaRotation > 0;
	// Return rotated
	return true;
}

} // namespace bareluge
