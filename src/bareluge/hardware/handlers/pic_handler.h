#pragma once
#include "utilities/utilities.h"

// Low-level includes
#include "drivers/pic/pic.h"

namespace bareluge {

// Send to and receive data from the PIC (type PIC24FJ256)

enum class PicMessage : uint8_t {
	ButtonLedOff = 152,        // Start-index to send to the pic to turn a button led off
	ButtonLedOn = 188,         // Start-index to send to the pic to turn a button led on
	OledSpiPinsSelect = 248,   // Received when the spi pins for the oled display are selected
	OledSpiPinsDeselect = 249, // Received then the spi pins for the oled display are deselected
	NextPressIsRelease = 252   // Indicates the next ui press message is a release instead
};

enum class PicMessageTarget : uint8_t { SpiHandler, UI };

// Class definition
class PicHandler {
public:
	// Receive one byte from the pic and save where it should be sent. Returns whether a byte was available to read
	bool getPicIndex(uint8_t& picIndex, PicMessageTarget& picMsgTarget);

	// Forwarding a few commands directly to the existing low-level pic code, so that this class is the only point
	// where the Bareluge firmware touches the existing low-level pic code. We might want to have a look at these later
	// and see whether we want to clean up or rewrite these

	void send(PicMessage message, uint8_t offset = 0);
	int32_t uartGetTxBufferSpace_(int32_t item);
	void setColourForTwoColumns(size_t idx, const std::array<RGB, kDisplayHeight * 2>& colours);
	void setGoldKnobIndicator(bool which, const std::array<uint8_t, kNumGoldKnobIndicatorLEDs>& indicator);
	void flush();
};

} // namespace bareluge
