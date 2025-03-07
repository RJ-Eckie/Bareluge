#include "pic_handler.h"

namespace bareluge {

bool PicHandler::getPicIndex(uint8_t& picIndex, PicMessageTarget& picMsgTarget) {
	// Get a picIndex from the uart
	if (!uartGetChar(UART_ITEM_PIC, reinterpret_cast<char*>(&picIndex))) {
		// Exit if uart was empty
		return false;
	}
	// All ui presses happen under index 180
	if (picIndex < 180) {
		picMsgTarget = PicMessageTarget::UI;
		return true;
	}
	// Individual messages:
	switch (picIndex) {
	case to_underlying(PicMessage::NextPressIsRelease):
		picMsgTarget = PicMessageTarget::UI;
		return true;
	case to_underlying(PicMessage::OledSpiPinsSelect):
	case to_underlying(PicMessage::OledSpiPinsDeselect):
		picMsgTarget = PicMessageTarget::SpiHandler;
		return true;
		break;
	}
	// We should not receive any other indeces
	D_PRINT("Warning: unhandled pic index: %d", picIndex);
	return true;
}

void PicHandler::send(PicMessage message, uint8_t offset) {
	PIC::send(to_underlying(message) + offset);
}

int32_t PicHandler::uartGetTxBufferSpace_(int32_t item) {
	return uartGetTxBufferSpace(item);
}

void PicHandler::setColourForTwoColumns(size_t idx, const std::array<RGB, kDisplayHeight * 2>& colours) {
	PIC::setColourForTwoColumns(idx, colours);
}

void PicHandler::setGoldKnobIndicator(bool which, const std::array<uint8_t, kNumGoldKnobIndicatorLEDs>& indicator) {
	PIC::setGoldKnobIndicator(which, indicator);
}

void PicHandler::flush() {
	PIC::flush();
}

} // namespace bareluge
