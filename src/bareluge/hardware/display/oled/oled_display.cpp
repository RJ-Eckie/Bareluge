#include "oled_display.h"

namespace bareluge {

void Oled::drawHorLine(uint8_t y, uint8_t x, uint8_t length) {
	if (y >= Height || length == 0)
		return;
	for (uint8_t lineX = x; lineX < x + length; lineX++) {
		if (lineX >= Width)
			return;
		setPixel(lineX, y);
	}
}

void Oled::drawVerLine(uint8_t x, uint8_t y, uint8_t length) {
	if (x >= Width || length == 0)
		return;
	for (uint8_t lineY = y; lineY <= y + length; lineY++) {
		if (lineY >= Height)
			return;
		setPixel(x, lineY);
	}
}

void Oled::drawLine(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2) {
	int8_t dx = abs(x2 - x1), dy = abs(y2 - y1);
	int8_t sx = (x1 < x2) ? 1 : -1, sy = (y1 < y2) ? 1 : -1;
	int8_t err = dx - dy;
	int8_t e2;
	while (x1 < Width && y1 < Height && (x1 != x2 || y1 != y2)) {
		setPixel(x1, y1);
		e2 = err * 2;
		if (e2 > -dy) {
			err -= dy;
			x1 += sx;
		}
		if (e2 < dx) {
			err += dx;
			y1 += sy;
		}
	}
}

void Oled::drawRect(uint8_t x, uint8_t y, uint8_t width, uint8_t height) {
	// Either dimension is zero
	if (width * height == 0) {
		return;
	}
	// Top
	drawHorLine(y, x, width);
	// Just one pixel high
	if (height == 1) {
		return;
	}
	// Left
	drawVerLine(x, y + 1, height - 2);
	// Just one pixel wide
	if (width == 1) {
		return;
	}
	// Bottom
	drawHorLine(y + height - 1, x, width);
	// Right
	drawVerLine(x + width - 1, y + 1, height - 2);
}

void Oled::fillRect(uint8_t x, uint8_t y, uint8_t width, uint8_t height) {
	for (uint8_t lineY = y; lineY < y + height; lineY++) {
		if (lineY >= Height) {
			return;
		}
		drawHorLine(lineY, x, width);
	}
}

void Oled::drawCorners(uint8_t x, uint8_t y, uint8_t width, uint8_t height, uint8_t cornerWidth) {
	drawHorLine(0, 0, cornerWidth);
	drawHorLine(0, width - 1 - cornerWidth, cornerWidth);
	drawHorLine(height - 1, 0, cornerWidth);
	drawHorLine(height - 1, width - 1 - cornerWidth, cornerWidth);
	drawVerLine(0, 0, cornerWidth);
	drawVerLine(0, height - 1 - cornerWidth, cornerWidth);
	drawVerLine(width - 1, 0, cornerWidth);
	drawVerLine(width - 1, height - 1 - cornerWidth, cornerWidth);
}

} // namespace bareluge
