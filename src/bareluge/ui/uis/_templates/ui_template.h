#pragma once
#include "ui/ui.h"

namespace bareluge {

// Template for creating new UIs

class UITemplate : public UI {
public:
	// Constructor
	UITemplate(PicHandler& picHandler, SpiHandler& spiHandler) : UI(picHandler, spiHandler) {}

	// Called when ui is created
	void init() override;

	// == UI behavior == //

	// Called every frame
	void handleRepeatingActions() override;

	// Called right before oled is rendered
	void drawOled() override;
	// Called right before pads are rendered
	void drawPads() override;

	// Called on button press or release
	void handleButtonPress(uint8_t index, bool pressed) override;
	// Called on encoder press or release
	void handleEncoderPress(uint8_t index, bool pressed) override;
	// Called on encoder rotation
	void handleEncoderRotation(uint8_t index, bool clockwise) override;
	// Called on pad press or release in main grid
	void handleMainGridPress(uint8_t x, uint8_t y, bool pressed) override;
	// Called on pad press or release in sidebar
	void handleSidebarPress(bool isRightColumn, uint8_t y, bool pressed) override;

	// == Further UI implementation below this line == //
};

} // namespace bareluge
