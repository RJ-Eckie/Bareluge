#pragma once
#include "utilities/matrix.h"

namespace bareluge {

// Base class for a grid of rgb pads

class Grid {
public:
	// Constructor
	Grid(uint8_t width, uint8_t height) : matrix(width, height) {}

	// Matrix implementation

	uint8_t toIndex(uint8_t x, uint8_t y) const { return matrix.toIndex(x, y); }
	uint8_t toX(uint8_t index) const { return matrix.toX(index); }
	uint8_t toY(uint8_t index) const { return matrix.toY(index); }

	uint8_t getWidth() const { return matrix.getWidth(); }
	uint8_t getHeight() const { return matrix.getHeight(); }
	uint8_t getSize() const { return matrix.getSize(); }
	bool contains(uint8_t x, uint8_t y) const { return matrix.contains(x, y); }

	// Dealing with pads

	virtual bool isPressed(uint8_t x, uint8_t y) const = 0;
	virtual uint32_t getHoldTime(uint8_t x, uint8_t y) const = 0;
	virtual void resetHold(uint8_t x, uint8_t y) = 0;
	virtual void disableHold(uint8_t x, uint8_t y) = 0;

	// Dealing with leds

	virtual RGB getColor(uint8_t x, uint8_t y) const = 0;
	virtual void setColor(uint8_t x, uint8_t y, RGB color) = 0;
	virtual void fill(RGB fillColor) {
		for (uint8_t x = 0; x < getWidth(); x++) {
			for (uint8_t y = 0; y < getHeight(); y++) {
				setColor(x, y, fillColor);
			}
		}
	}
	void clear() { fill(RGB{}); }

protected:
	// Properties
	Matrix matrix;
};

} // namespace bareluge
