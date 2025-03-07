#pragma once

#include "hardware/handlers/pic_handler.h"
#include "hardware/handlers/spi_handler.h"
#include "ui/ui.h"
#include "utilities/utilities.h"

namespace bareluge {

// Base class representing the Deluge, only has static members

class Deluge {
public:
	// Main entry point
	static void launch();

	// Interrupt handler
	static void isrOledDataTransferComplete(uint32_t handler);

private:
	// Setting this to a few seconds can help to catch sysex debugging messages during the boot sequence
	static constexpr uint32_t bootDelay = TK::toSamples(0);

	// Sub-systems

	static SpiHandler spiHandler;
	static PicHandler picHandler;
	static UI* ui;

	// Setup
	static void init();

	// Main Loop
	static void mainLoop();

	// This receives messages from the PIC, which are then forwarded to either the SPI Handler or the UI
	static void handleOnePicByte();
};

} // namespace bareluge
