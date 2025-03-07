#include "view_ui_template.h"

namespace bareluge {

// Called when ui is created
void ViewUITemplate::init() {
	D_PRINT("Initializing ViewUI");
	// Launch the default view for this ViewUI
	launchView(ViewType::Base); // Replace this with your custom view type
}

// == UI behavior == //

// // Called every frame
// void ViewUITemplate::handleRepeatingActions() {
// 	// Implement view-independent repeating, non-blocking behavior
// }

// // Called right before oled is rendered
// void ViewUITemplate::drawOled() {
// 	// Use Oled methods to clear / draw on the oled display
// }

// // Called right before pads are rendered
// void ViewUITemplate::drawPads() {
// 	// Use clear() and setColor() methods on mainGrid and sideBar to draw the pads
// }

// Called on button press or release
void ViewUITemplate::handleButtonPress(uint8_t index, bool pressed) {
	D_PRINT("%s button %d, UI level", pressed ? "Pressed" : "Released", index);
	// Example code:
	switch (static_cast<ButtonName>(index)) {
	// Filter for a button
	case ButtonName::LevelPan:
		if (pressed) {
			// View-independent behavior for the level/pan button when pressed
		}
		else {
			// View-independent behavior for the level/pan button when released
		}
		// Exit
		return;
	}
	// As button presses/releases are no longer forwarded to the active view by default, we need to do that manually.
	// Because of the return statement above, interaction with the level/pan button has been effectively filtered out.
	// In this way you have control over which interactions are handled on a global level, and which interactions are
	// handled on view level. All non-level/pan button interactions get passed to the active view:
	view->handleButtonPress(index, pressed);
}

// // Called on encoder press or release
// void ViewUITemplate::handleEncoderPress(uint8_t index, bool pressed) {
// 	D_PRINT("%s encoder %d, UI level", pressed ? "Pressed" : "Released", index);
// }

// // Called on encoder rotation
// void ViewUITemplate::handleEncoderRotation(uint8_t index, bool clockwise) {
// 	D_PRINT("Encoder %d rotated %s, UI level", index, clockwise ? "clockwise" : "counter-clockwise");
// }

// // Called on pad press or release in main grid
// void ViewUITemplate::handleMainGridPress(uint8_t x, uint8_t y, bool pressed) {
// 	D_PRINT("%s pad (%d, %d) on main grid, UI level", pressed ? "Pressed" : "Released", x, y);
// }

// // Called on pad press or release in sidebar
// void ViewUITemplate::handleSidebarPress(bool isRightColumn, uint8_t y, bool pressed) {
// 	D_PRINT("%s pad %d in the %s column of the sidebar, UI level", pressed ? "Pressed" : "Released", y,
// 	        isRightColumn ? "right" : "left");
// }

// == Further UI implementation below this line == //

} // namespace bareluge
