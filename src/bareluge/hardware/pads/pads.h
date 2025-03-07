#pragma once
#include "grid.h"
#include "hardware/handlers/pic_handler.h"
#include "utilities/pressable.h"

namespace bareluge {

// Manages all rgb pads
//
// The Pads class will mostly not be called directly. Instead use MainGrid and Sidebar, who will in turn call Pads
//
// Note that pad colors are stored in a column major matrix, as the pic expects us to send the data for the rgb leds
// in a column major matrix format. Storing the colors like this eliminates the need for any loops in the rendering
// code - we can directly copy a block of data from padColorArray to the pic. PadIndex() provides the correct index
// calculation

class Pads : public Grid {
public:
	// Constant
	static constexpr uint8_t Width = 18;
	static constexpr uint8_t Height = 8;
	static constexpr uint8_t NumPads = Width * Height;

	// Static
	static constexpr uint8_t indexFromPicIndex(uint8_t picIndex) {
		// Out of bounds
		if (picIndex >= NumPads) {
			D_PRINT("Warning: pic index for pad out of bounds");
			return 255;
		}
		// The pic index needs some wrangling to end up as an intuitive linear index
		uint8_t x = (picIndex % (Width / 2)) * 2 + (picIndex / (Width / 2) >= Height);
		uint8_t y = (picIndex / (Width / 2)) % Height;
		return y * Width + x;
	}

	// Constructor
	Pads(PicHandler& picHandler) : Grid(Width, Height), picHandler(picHandler) {}

	// Dealing with pads

	bool isPressed(uint8_t x, uint8_t y) const override { return padArray[toIndex(x, y)].isPressed(); }
	uint32_t getHoldTime(uint8_t x, uint8_t y) const override { return padArray[toIndex(x, y)].getHoldTime(); }

	void press(uint8_t x, uint8_t y, bool pressed = true) { padArray[toIndex(x, y)].press(pressed); }
	void resetHold(uint8_t x, uint8_t y) override { padArray[toIndex(x, y)].resetHold(); }
	void disableHold(uint8_t x, uint8_t y) override { padArray[toIndex(x, y)].disableHold(); }

	// Dealing with leds

	RGB getColor(uint8_t x, uint8_t y) const override { return padColorArray[padIndex(x, y)]; }
	void setColor(uint8_t x, uint8_t y, RGB color) override {
		padColorArray[padIndex(x, y)] = color;
		blockNeedsRendering[x / 2] = true;
	}
	void fill(RGB color) override {
		padColorArray.fill(color);
		blockNeedsRendering.fill(true);
	}

	// Fill columns
	void fillColumns(uint8_t startColumn, uint8_t numColumns, RGB color) {
		if (startColumn + numColumns > Width) {
			D_PRINT("Warning: fillColumns parameters out of bounds");
		}
		// Keep writing within bounds
		uint8_t fillNumColumns = clamp(Width - startColumn, 0, numColumns);
		// Fill color array
		std::fill_n(padColorArray.begin() + startColumn * Height, fillNumColumns * Height, color);
		// Mark blocks as needing rendering
		std::fill_n(blockNeedsRendering.begin() + startColumn / 2, (fillNumColumns + 1) / 2, true);
	}

	// Main rendering method for pads
	void render() {
		// Rendering happens per block of two columns
		static constexpr uint8_t RenderBlockSize = 2 * Height;
		static constexpr uint8_t NumRenderBlocks = Width / 2;

		std::array<RGB, RenderBlockSize> renderBlock;
		uint8_t numBlocksRendered = 0;
		while (
		    // Stop when all blocks have been rendered
		    (numBlocksRendered < NumRenderBlocks) &&
		    // Stop when uart buffer does not have enough space left (taking 2x size copied from community)
		    (picHandler.uartGetTxBufferSpace_(UART_ITEM_PIC) >= (3 * RenderBlockSize) * 2)) {
			if (blockNeedsRendering[nextRenderBlock]) {
				// Copy a block of data representing two columns from padColorArray to renderBlock
				std::copy_n(padColorArray.begin() + nextRenderBlock * RenderBlockSize, RenderBlockSize,
				            renderBlock.begin());
				// Send renderBlock to PIC
				picHandler.setColourForTwoColumns(nextRenderBlock, renderBlock);
				// Mark as no longer needing rendering
				blockNeedsRendering[nextRenderBlock] = false;
			}
			// Next block
			numBlocksRendered++;
			nextRenderBlock = (nextRenderBlock + 1) % NumRenderBlocks;

			// If the uart buffer is too full halfway rendering the matrix, the other blocks are not sent. On the
			// next render frame, we continue rendering where we left off, at nextRenderBlock
		}
	}

private:
	// Properties
	PicHandler& picHandler;
	std::array<Pressable, NumPads> padArray{};         // The pressable pads
	std::array<RGB, NumPads> padColorArray{};          // The rgb leds
	uint8_t nextRenderBlock{};                         // Index of next block to render
	std::array<bool, Width / 2> blockNeedsRendering{}; // Was block updated since last render?

	// Helper
	uint8_t padIndex(uint8_t x, uint8_t y) const { return matrix.toIndex(x, y, true); }
};

} // namespace bareluge
