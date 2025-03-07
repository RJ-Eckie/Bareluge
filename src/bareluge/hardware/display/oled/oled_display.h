#pragma once
#include "hardware/handlers/spi_handler.h"

namespace bareluge {

// Oled display (type SSD1306)

class Oled {
private:
	// Constants
	constexpr static uint8_t DisplayHeight = 48;   // Height of the display that we render
	constexpr static uint8_t NumInvisibleRows = 5; // Number of rows from the top that are invisible

public:
	constexpr static uint8_t Width = 128;
	constexpr static uint8_t Height = DisplayHeight - NumInvisibleRows;  // Visible height
	constexpr static uint16_t DataSizeBytes = DisplayHeight * Width / 8; // Each byte holds eight pixels

	// Constructor
	Oled(SpiHandler& spiHandler) : spiHandler(spiHandler) {}

	// Fast pixel methods - no bounds checking for speed reasons. Top left = [0, 0]

	// Pixels are shifted down to the first visible row
	constexpr void setPixel(uint8_t x, uint8_t y, bool on = true) { setPixelAbs(x, y + NumInvisibleRows, on); }
	// Even faster, these don't map the top offset
	constexpr void setPixelAbs(uint8_t x, uint8_t y, bool on = true) {
		on ? bufferPage[!displayedPage][((y >> 3) << 7) + x] |= (1 << (y & 7))
		   : bufferPage[!displayedPage][((y >> 3) << 7) + x] &= ~(1 << (y & 7));
	}

	// Clear non-displayed buffer page
	void clear() { bufferPage[!displayedPage].fill(0); }

	// Only give UI access to rendering method
	class AccessKey {
	private:
		friend class UI;
		AccessKey() {}
	};

	// Main rendering method
	void render(AccessKey) {
		// Switch the displayed buffer page
		displayedPage = !displayedPage;
		// Send displayed page to the spi handler
		spiHandler.addToQueue(SpiDestination::Oled, bufferPage[displayedPage].data());
		// The non-displayed buffer page mirrors the displayed buffer page
		bufferPage[!displayedPage] = bufferPage[displayedPage];
	}

	// Draw methods - these are just some quick examples for now. It would maybe make sense to implement an
	// existing lightweight graphics library here

	void drawHorLine(uint8_t y, uint8_t x = 0, uint8_t length = Width);
	void drawVerLine(uint8_t x, uint8_t y = 0, uint8_t length = Height);
	void drawLine(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2);
	void drawRect(uint8_t x, uint8_t y, uint8_t width, uint8_t height);
	void fillRect(uint8_t x, uint8_t y, uint8_t width, uint8_t height);
	void drawCorners(uint8_t x, uint8_t y, uint8_t width, uint8_t height, uint8_t cornerWidth);

private:
	// Properties
	SpiHandler& spiHandler;
	std::array<std::array<uint8_t, DataSizeBytes>, 2> bufferPage{};
	bool displayedPage{}; // The page that is not displayed is open for writing
};

} // namespace bareluge
