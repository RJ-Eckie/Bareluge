#pragma once
#include "ui/ui.h"

namespace bareluge {

// A simple UI implementation that tests all hardware controls
//
// - Every input-control outputs a message over sysex logging when pressed/rotated
// - Pressing any pad or button toggles its led
// - The oled display shows a crosshair with a box at its center
//   - Turning the Vertical/Horizontal encoders moves the crosshair
//   - Turning the Tempo encoder changes the size of the box
//   - Pressing any of these encoders resets its parameter to a default value
// - The gold encoders adjust the value displayed on their led bars
//   - Pressing a gold encoder switches between showing the value as a unipolar and as a bipolar value
// - The mod-button leds (Level/Pan through Custom2/Custom3) represent a binary value, which can be adjusted by rotating
// the Select encoder
//   - Pressing the Select encoder resets the value to zero
//   - The buttons can be pressed to toggle bits in the value

class HardwareTestUI : public UI {
public:
	// Constructor
	HardwareTestUI(PicHandler& picHandler, SpiHandler& spiHandler) : UI(picHandler, spiHandler) {}

	// Called when view is created
	void init() override;

	// Called right before oled is rendered
	void drawOled() override;

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

private:
	// Constant
	static constexpr uint8_t NotchDelayFrames = 8;
	static constexpr uint8_t DefaultBoxSize = 12;
	static constexpr uint8_t DefaultOledLineX = Oled::Width / 2;
	static constexpr uint8_t DefaultOledLineY = Oled::Height / 2;

	// Properties
	uint8_t oledLineX{DefaultOledLineX};
	uint8_t oledLineY{DefaultOledLineY};
	uint8_t oledBoxSize{DefaultBoxSize};
	std::array<bool, Button::NumButtons> buttonLit{};     // Is a button lit?
	std::array<bool, MainGrid::NumPads> mainGridPadLit{}; // Is a pad in the main grid lit?
	std::array<bool, Sidebar::NumPads> sidebarPadLit{};   // Is a pad in the sidebar lit?
	std::array<uint8_t, 2> goldEncoderValue{128, 128};    // Value displayed on gold encoder led bar
	std::array<bool, 2> ledBarUnipolar{true, true};       // Led bar display mode
	std::array<uint8_t, 2> goldEncoderNotchDelay{}; // Used to create a small notch in the center of a bipolar value
	uint8_t modButtonsBinaryValue{};                // Value displayed on the mod button leds
};

} // namespace bareluge
