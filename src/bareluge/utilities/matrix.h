#pragma once
#include "utilities/utilities.h"

namespace bareluge {

// The Matrix class provides utilities for using a 1D array to represent a 2D matrix
//
// By default, items are ordered row first (row major) but column major calculations can be selected by setting the
// columnMajor flag

class Matrix {
public:
	// Constructor
	Matrix(uint8_t width, uint8_t height) : width(width), height(height) {}

	// Mapping between 2D coordinates and linear index

	static constexpr uint8_t toIndex(uint8_t x, uint8_t y, uint8_t width, uint8_t height, bool columnMajor = false) {
		x %= width;
		y %= height;
		return columnMajor ? x * height + y : y * width + x;
	}
	static constexpr uint8_t toX(uint8_t index, uint8_t width, uint8_t height, bool columnMajor = false) {
		return (columnMajor ? index / height : index) % width;
	}
	static constexpr uint8_t toY(uint8_t index, uint8_t width, uint8_t height, bool columnMajor = false) {
		return (columnMajor ? index : index / width) % height;
	}

	uint8_t toIndex(uint8_t x, uint8_t y, bool columnMajor = false) const {
		return toIndex(x, y, width, height, columnMajor);
	}
	uint8_t toX(uint8_t index, bool columnMajor = false) const { return toX(index, width, height, columnMajor); }
	uint8_t toY(uint8_t index, bool columnMajor = false) const { return toY(index, width, height, columnMajor); }

	// Properties

	uint8_t getWidth() const { return width; }
	uint8_t getHeight() const { return height; }
	uint8_t getSize() const { return width * height; }
	bool contains(uint8_t x, uint8_t y) const { return x < width && y < height; }

private:
	// Properties
	uint8_t width;
	uint8_t height;
};

} // namespace bareluge
