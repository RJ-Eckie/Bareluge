#pragma once
#include "hardware/hardware_includes.h"
#include "hsv.h"
#include "ui/views/view.h"
#include "utilities/utilities.h"

namespace bareluge::TD01 {

// Handles the color picker which includes the sidebar, the gold encoders and the mod buttons

class ColorPicker {
public:
	// Constructor
	ColorPicker(UiElements& uiElements)
	    : sidebar(uiElements.sidebar), saturationLedBar(uiElements.getGoldEncoder(false).getLedBar()),
	      valueLedBar(uiElements.getGoldEncoder(true).getLedBar()) {
		uiElements.getButtonRange<8>(modButtons);
	}

	void init() { redraw(); }

	// Properties

	RGB getActiveColor() { return HSV::HSVtoRGB(selectedColor[rightColorActive]); }
	HSV getActiveHSV() { return selectedColor[rightColorActive]; }
	uint8_t getActiveHueOffset() { return hueOffset[rightColorActive]; }

	// Saturation

	void setActiveSaturation(uint8_t newSaturation) {
		selectedColor[rightColorActive].s = newSaturation;
		redraw();
	}

	void changeActiveSaturation(bool increasing) {
		setActiveSaturation(clamp(selectedColor[rightColorActive].s + (increasing ? 1 : -1), 0, 255));
	}

	void setDefaultSaturation() {
		setActiveSaturation(192); // Default: 75%
	}

	// Value

	void setActiveValue(uint8_t newValue) {
		selectedColor[rightColorActive].v = newValue;
		redraw();
	}

	void changeActiveValue(bool increasing) {
		setActiveValue(clamp(selectedColor[rightColorActive].v + (increasing ? 1 : -1), 0, 255));
	}

	void setDefaultValue() {
		setActiveValue(192); // Default: 75%
	}

	// Hue Offset

	void setHueOffset(uint8_t newHueOffset) {
		newHueOffset = min(newHueOffset, 7 * HuePerModButton);
		selectedColor[rightColorActive].h += newHueOffset - hueOffset[rightColorActive];
		hueOffset[rightColorActive] = newHueOffset;
		redraw();
	}

	void changeHueOffset(bool increasing) {
		setHueOffset(max(hueOffset[rightColorActive] + (increasing ? 1 : -1) * HuePerModButton, 0));
	}

	void setDefaultHueOffset() { setHueOffset(0); }

	// Press

	void handleSidebarPress(bool isRightColumn, uint8_t y, bool pressed) {
		if (pressed) {
			// Pick a hue
			if (y < PickerHeight) {
				selectedColor[rightColorActive].h = hueFromPad(isRightColumn, y);
			}
			// Pick which color is active
			else {
				rightColorActive = isRightColumn;
			}
			// Redraw
			redraw();
		}
	}

private:
	// Constant
	static constexpr std::array<HSV, 2> StartColors = {{{32, 192, 192}, {176, 192, 192}}};
	static constexpr uint8_t PickerHeight = 6;                  // Vertical pads showing hues
	static constexpr uint8_t NumHues = 2 * PickerHeight;        // Amount of hues on those pads
	static constexpr uint8_t HuePerPad = HSV::MaxHue / NumHues; // Hue difference between two pads
	static constexpr uint8_t HuePerModButton = HuePerPad / 8;   // Hue fine offset per select encoder click

	// UI references
	Sidebar& sidebar;
	LedBar& saturationLedBar;
	LedBar& valueLedBar;
	std::array<Button*, 8> modButtons;

	// Properties
	bool rightColorActive{};
	std::array<HSV, 2> selectedColor{StartColors};
	std::array<uint8_t, 2> hueOffset{};

	// The sidebar is only 16 pads, so we just fully redraw it even if only a single pad has changed
	void redraw() {
		// Hue picker
		for (uint8_t y = 0; y < PickerHeight; y++) {
			sidebar.setColor(0, y, colorFromPad(0, y));
			sidebar.setColor(1, y, colorFromPad(1, y));
		}
		// Selector
		sidebar.setColor(0, 6, RGB::monochrome(rightColorActive ? 32 : 192));
		sidebar.setColor(1, 6, RGB::monochrome(rightColorActive ? 192 : 32));
		// Preview
		sidebar.setColor(0, 7, HSV::HSVtoRGB(selectedColor[0]));
		sidebar.setColor(1, 7, HSV::HSVtoRGB(selectedColor[1]));
		// Led bars
		saturationLedBar.setValue(getActiveHSV().s);
		valueLedBar.setValue(getActiveHSV().v);
		// Mod buttons
		for (uint8_t i = 0; i < 8; i++) {
			modButtons[i]->setLed(getActiveHueOffset() / HuePerModButton >= i);
		}
	}

	RGB colorFromPad(bool isRightColumn, uint8_t y) {
		return RGB::fromHue(hueFromPad(isRightColumn, y))
		    .dim(hueFromPad(isRightColumn, y) == selectedColor[rightColorActive].h ? 0 : 3);
	}

	uint8_t hueFromPad(bool isRightColumn, uint8_t y) {
		// Keep y within bounds
		y %= PickerHeight;
		// Left column
		if (!isRightColumn) {
			return y * HuePerPad + hueOffset[rightColorActive];
		}
		// Right column
		return (NumHues - 1 - y) * HuePerPad + hueOffset[rightColorActive];
	}
};

} // namespace bareluge::TD01
