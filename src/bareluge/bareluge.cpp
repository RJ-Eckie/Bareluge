#include "bareluge.h"
#include "ui/uis/ui_list.h"

// Low-level includes
#include "OSLikeStuff/timers_interrupts/timers_interrupts.h"

// Include from community
#include "playback/playback_handler.h" // For system timer and dev midi comms

namespace bareluge {

// Set the UI (when writing a custom UI, change this to your UI type)
UI* Deluge::ui = new ViewLauncherUI(Deluge::picHandler, Deluge::spiHandler);

// Init static properties
PicHandler Deluge::picHandler = PicHandler();
SpiHandler Deluge::spiHandler = SpiHandler(Deluge::picHandler);

void Deluge::launch() {
	// This delay loop can be used to give the developer time to turn on sysex-logging after downloading a new firmware
	// to the Deluge, and still catch debug messages that are generated during the bareluge boot sequence
	uint32_t loggedTime;
	while (!TK::hasPassed(bootDelay)) {
		// Make sure the timer loop keeps running
		AudioEngine::routineWithClusterLoading();
		// Give a sign of life every 500ms
		if (TK::doEvery(TK::toSamples(500), loggedTime)) {
			D_PRINT("Boot Delay Loop");
		}
	}

	// Initialize Bareluge firmware
	D_PRINT("Starting Bareluge Init");
	init();

	// Start the main loop
	D_PRINT("Starting Bareluge Main Loop");
	mainLoop();
}

void Deluge::isrOledDataTransferComplete(uint32_t handler) {
	spiHandler.isrTransferComplete();
}

void Deluge::init() {
	// Setup interrupt for spi handler
	setupAndEnableInterrupt(isrOledDataTransferComplete, INTC_ID_DMAINT0 + OLED_SPI_DMA_CHANNEL, 13);
	// Initialise UI
	ui->baseInit(UI::AccessKey());
}

void Deluge::mainLoop() {
	while (true) {
		// Spi handler, needs regular execution to keep spi transfers going
		spiHandler.handleRepeatingActions();
		// Pic handler, receives ui press-input and some SPI commands
		handleOnePicByte();

		// Run UI frame, this handles all bareluge behavior
		ui->baseHandleRepeatingActions(UI::AccessKey());

		// Flush PIC - RJ: it doesn't feel very efficient to just call this every frame regardless of what happened, but
		// I've not been able to outline exactly when this does and does not need to happen. I know the buttons won't
		// update their led states if this isn't called regularly so we're leaving it here for now, until a more elegant
		// solution presents itself
		picHandler.flush();

		// This enables midi sysex for usb firmware upload and debug logging (from community)
		playbackHandler.routine();
		// This runs the system timer (from community)
		AudioEngine::routineWithClusterLoading();
	}
}

void Deluge::handleOnePicByte() {
	uint8_t picIndex;
	PicMessageTarget picMsgTarget;
	if (picHandler.getPicIndex(picIndex, picMsgTarget)) {
		switch (picMsgTarget) {
		case PicMessageTarget::SpiHandler:
			spiHandler.handlePicIndex(picIndex);
			break;
		case PicMessageTarget::UI:
			ui->handlePicIndex(picIndex, UI::AccessKey());
			break;
		}
	}
}

} // namespace bareluge
