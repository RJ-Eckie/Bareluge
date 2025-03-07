#pragma once
#include "grid.h"
#include "pads.h"

namespace bareluge {

// The main 16 x 8 grid of rgb pads

class MainGrid : public Grid {
public:
	// Constant
	static constexpr uint8_t Width = 16;
	static constexpr uint8_t Height = 8;
	static constexpr uint8_t NumPads = Width * Height;

	// Static
	static constexpr uint8_t toIndex(uint8_t x, uint8_t y) { return Matrix::toIndex(x, y, Width, Height); }
	static constexpr uint8_t toX(uint8_t index) { return Matrix::toX(index, Width, Height); }
	static constexpr uint8_t toY(uint8_t index) { return Matrix::toY(index, Width, Height); }

	// Constructor
	MainGrid(Pads& pads) : Grid(Width, Height), pads(pads) {}

	// Dealing with pads

	bool isPressed(uint8_t x, uint8_t y) const override { return pads.isPressed(x, y); }
	uint32_t getHoldTime(uint8_t x, uint8_t y) const override { return pads.getHoldTime(x, y); }
	void resetHold(uint8_t x, uint8_t y) override { pads.resetHold(x, y); }
	void disableHold(uint8_t x, uint8_t y) override { pads.disableHold(x, y); }

	// Dealing with leds

	RGB getColor(uint8_t x, uint8_t y) const override { return pads.getColor(x, y); }
	void setColor(uint8_t x, uint8_t y, RGB color) override { pads.setColor(x, y, color); }
	void fill(RGB fillColor) override { pads.fillColumns(0, Width, fillColor); }

private:
	// Reference to pads
	Pads& pads;
};

} // namespace bareluge
