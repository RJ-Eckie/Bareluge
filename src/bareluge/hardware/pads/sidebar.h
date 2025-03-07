#pragma once
#include "grid.h"
#include "pads.h"

namespace bareluge {

// The 2 x 8 grid of rgb pads to the right of the main grid

class Sidebar : public Grid {
public:
	// Constant
	static constexpr uint8_t Width = 2;
	static constexpr uint8_t Height = 8;
	static constexpr uint8_t NumPads = Width * Height;

	// Static
	static constexpr uint8_t toIndex(uint8_t x, uint8_t y) { return Matrix::toIndex(x, y, Width, Height); }
	static constexpr uint8_t toX(uint8_t index) { return Matrix::toX(index, Width, Height); }
	static constexpr uint8_t toY(uint8_t index) { return Matrix::toY(index, Width, Height); }

	// Constructor
	Sidebar(Pads& pads) : Grid(Width, Height), pads(pads) {}

	// Dealing with pads

	bool isPressed(uint8_t x, uint8_t y) const override { return pads.isPressed(x + XOffset, y); }
	uint32_t getHoldTime(uint8_t x, uint8_t y) const override { return pads.getHoldTime(x + XOffset, y); }
	void resetHold(uint8_t x, uint8_t y) override { pads.resetHold(x + XOffset, y); }
	void disableHold(uint8_t x, uint8_t y) override { pads.disableHold(x + XOffset, y); }

	// Dealing with leds

	RGB getColor(uint8_t x, uint8_t y) const override { return pads.getColor(x + XOffset, y); }
	void setColor(uint8_t x, uint8_t y, RGB color) override { pads.setColor(x + XOffset, y, color); }
	void fill(RGB fillColor) override { pads.fillColumns(XOffset, Width, fillColor); }

private:
	// Constant
	static constexpr uint8_t XOffset = 16;

	// Reference to pads
	Pads& pads;
};

} // namespace bareluge
