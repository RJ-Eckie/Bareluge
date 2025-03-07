#pragma once
#include "hardware/hardware_includes.h"

namespace bareluge::TD01 {

// Displays a representation of the main grid on the oled display

class CanvasViewer {
private:
	// In each direction, how may pixels does one pad occupy on the oled?
	static constexpr uint8_t ZoomFactor = 2;
	// Thickness of the outline around the main grid
	static constexpr uint8_t OutlineThickness = 1;

public:
	// How many pads can we display?
	static constexpr uint8_t PadWidth = (Oled::Width - 2 * OutlineThickness) / ZoomFactor;
	static constexpr uint8_t PadHeight = (Oled::Height - 2 * OutlineThickness) / ZoomFactor;

	// Constructor
	CanvasViewer(Oled& oled) : oled(oled) {}

	void setColor(uint8_t padX, uint8_t padY, RGB newColor) {
		uint8_t screenX = padX * ZoomFactor + OutlineThickness;
		uint8_t screenY = maxY - ((padY + 1) * ZoomFactor + OutlineThickness); // Vertically mirrored with height maxY
		uint8_t brightness = log_curve(max(max(newColor.r, newColor.g), newColor.b), 255);
		// Current implementation only works for ZoomFactor = 2
		oled.setPixel(screenX, screenY, brightness >= 1 * 255 / 5);
		oled.setPixel(screenX + 1, screenY + 1, brightness >= 2 * 255 / 5);
		oled.setPixel(screenX, screenY + 1, brightness >= 3 * 255 / 5);
		oled.setPixel(screenX + 1, screenY, brightness >= 4 * 255 / 5);
	}

	void redraw(std::array<std::array<RGB, PadHeight>, PadWidth>& padColor, uint8_t xOffset, uint8_t yOffset) {
		// Size of the outline around the main grid
		static constexpr uint8_t OutlineWidth = MainGrid::Width * ZoomFactor + 2 * OutlineThickness;
		static constexpr uint8_t OutlineHeight = MainGrid::Height * ZoomFactor + 2 * OutlineThickness;

		// Clear buffer
		oled.clear();
		// Draw pads
		for (uint8_t y = 0; y < PadHeight; y++) {
			for (uint8_t x = 0; x < PadWidth; x++) {
				setColor(x, y, padColor[x][y]);
			}
		}
		// Draw canvas corners
		oled.drawCorners(0, 0, Oled::Width, maxY, 4);
		// Draw main grid outline, vertically mirrored with height maxY
		// Currently only works for OutlineThickness = 1
		oled.drawRect(ZoomFactor * xOffset, maxY - ZoomFactor * yOffset - OutlineHeight, OutlineWidth, OutlineHeight);
	}

private:
	// We're vertically mirroring the display to reflect the orientation of the main grid. The display is 43px tall but
	// we use 42 for convenience
	static constexpr uint8_t maxY = 42;
	// Properties
	Oled& oled;
};

} // namespace bareluge::TD01
