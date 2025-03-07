#pragma once
#include "utilities/utilities.h"

namespace bareluge {

// Utility base class for pressable elements

class Pressable {
public:
	// Get state
	bool isPressed() const { return pressed; }

	// How long is this element being held?
	uint32_t getHoldTime() const {
		return (isPressed() && holdEnabled) ? TK::timeSince(lastPressed) : 0;
	}

	// Press / release
	void press(bool pressed = true) {
		this->pressed = pressed;
		if (pressed) {
			holdEnabled = true;
			TK::log(lastPressed);
		}
	}

	// Call this to reset the holdtime
	void resetHold() {
		holdEnabled = true;
		TK::log(lastPressed);
	}

	// Call this if the pressable can not be used for another action relying on getHoldTime()
	void disableHold() { holdEnabled = false; }

private:
	// Properties
	bool pressed{};
	uint32_t lastPressed{};
	bool holdEnabled{};
};

} // namespace bareluge
