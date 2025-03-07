#include "ui_template.h"

namespace bareluge {

// Called when ui is created
void UITemplate::init() {
	D_PRINT("Initializing UI");
}

// == UI behavior == //

// Called every frame
void UITemplate::handleRepeatingActions() {
	// Implement repeating, non-blocking behavior
}

// Called right before oled is rendered
void UITemplate::drawOled() {
	// Use Oled methods to clear / draw on the oled display
}

// Called right before pads are rendered
void UITemplate::drawPads() {
	// Use clear() and setColor() methods on mainGrid and sideBar to draw the pads
}

// Called on button press or release
void UITemplate::handleButtonPress(uint8_t index, bool pressed) {
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
void UITemplate::handleEncoderPress(uint8_t index, bool pressed) {
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
void UITemplate::handleEncoderRotation(uint8_t index, bool clockwise) {
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
void UITemplate::handleMainGridPress(uint8_t x, uint8_t y, bool pressed) {
	D_PRINT("%s pad (%d, %d) on main grid", pressed ? "Pressed" : "Released", x, y);
	if (pressed) {
		// Behavior when pressed
	}
	else {
		// Behavior when released
	}
}

// Called on pad press or release in sidebar
void UITemplate::handleSidebarPress(bool isRightColumn, uint8_t y, bool pressed) {
	D_PRINT("%s pad %d in the %s column of the sidebar", pressed ? "Pressed" : "Released", y,
	        isRightColumn ? "right" : "left");
	if (pressed) {
		// Behavior when pressed
	}
	else {
		// Behavior when released
	}
}

// == Further UI implementation below this line == //

} // namespace bareluge
