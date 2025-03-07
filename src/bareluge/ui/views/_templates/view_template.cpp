#include "view_template.h"

namespace bareluge {

// Called when view is created
void ViewTemplate::init() {
	D_PRINT("Initializing view");
}

// Called right before view is destroyed
void ViewTemplate::deinit() {
	D_PRINT("De-initializing view");
}

// == View behavior == //

// Called every frame
void ViewTemplate::handleRepeatingActions() {
	// Implement repeating, non-blocking behavior
}

// Called right before oled is rendered
void ViewTemplate::drawOled() {
	// Use Oled methods to draw on / clear the oled display
}

// Called right before pads are rendered
void ViewTemplate::drawPads() {
	// Use clear() and setColor() methods on mainGrid and sideBar to draw the pads
}

// Called on button press or release
void ViewTemplate::handleButtonPress(uint8_t index, bool pressed) {
	D_PRINT("%s button %d", pressed ? "Pressed" : "Released", index);
	switch (static_cast<ButtonName>(index)) {
	// Filter for a button
	case ButtonName::LevelPan:
		if (pressed) {
			// Behavior when pressed
		}
		else {
			// Behavior when released
		}
	}
}

// Called on encoder press or release
void ViewTemplate::handleEncoderPress(uint8_t index, bool pressed) {
	D_PRINT("%s encoder %d", pressed ? "Pressed" : "Released", index);
	switch (static_cast<EncoderName>(index)) {
	// Filter for an encoder
	case EncoderName::Vertical:
		if (pressed) {
			// Behavior when pressed
		}
		else {
			// Behavior when released
		}
		break;
	}
}

// Called on encoder rotation
void ViewTemplate::handleEncoderRotation(uint8_t index, bool clockwise) {
	D_PRINT("Encoder %d rotated %s", index, clockwise ? "clockwise" : "counter-clockwise");
	switch (static_cast<EncoderName>(index)) {
	// Filter for an encoder
	case EncoderName::Vertical:
		if (clockwise) {
			// Behavior when rotated clockwise
		}
		else {
			// Behavior when rotated counter-clockwise
		}
		break;
	}
}

// Called on pad press or release in main grid
void ViewTemplate::handleMainGridPress(uint8_t x, uint8_t y, bool pressed) {
	D_PRINT("%s pad (%d, %d) on main grid", pressed ? "Pressed" : "Released", x, y);
	if (pressed) {
		// Behavior when pressed
	}
	else {
		// Behavior when released
	}
}

// Called on pad press or release in sidebar
void ViewTemplate::handleSidebarPress(bool isRightColumn, uint8_t y, bool pressed) {
	D_PRINT("%s pad %d in the %s column of the sidebar", pressed ? "Pressed" : "Released", y,
	        isRightColumn ? "right" : "left");
	if (pressed) {
		// Behavior when pressed
	}
	else {
		// Behavior when released
	}
}

// == Further view implementation below this line == //

} // namespace bareluge
