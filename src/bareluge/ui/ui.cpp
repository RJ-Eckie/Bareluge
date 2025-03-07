#include "ui.h"

namespace bareluge {

void UI::baseInit(AccessKey) {
	D_PRINT("Base-initializing UI");
	// This offsets oled rendering to not execute on the same frames as pads rendering
	lastPadsRenderTime = TK::time();
	lastOledRenderTime = TK::time() + TK::frameDuration(OledFPS) / 2;
	// Clear UI
	clearUI();
	// Call the virtual init
	init();
}

void UI::baseHandleRepeatingActions(AccessKey) {
	// Receive encoder input
	readEncoderRotations();
	// Process encoder input
	processEncoderRotations();
	// Render Oled
	if (TK::doEveryFps(OledFPS, lastOledRenderTime)) {
		drawOled();
		oled.render(Oled::AccessKey());
	}
	// Render pads
	if (TK::doEveryFps(PadsFPS, lastPadsRenderTime)) {
		drawPads();
		pads.render();
	}
	// Call the virtual handleRepeatingActions
	handleRepeatingActions();
}

void UI::handlePicIndex(uint8_t picIndex, AccessKey) {
	// Release indicator
	if (picIndex == to_underlying(PicMessage::NextPressIsRelease)) {
		// The next press message we receive is actually a receive message
		nextPressIsRelease = true;
		return;
	}
	uint8_t elementIndex;
	// Pads (numbered 0 through NumPads - 1)
	if (picIndex < Pads::NumPads) {
		elementIndex = Pads::indexFromPicIndex(picIndex);
		uint8_t padX = Matrix::toX(elementIndex, Pads::Width, Pads::Height);
		uint8_t padY = Matrix::toY(elementIndex, Pads::Width, Pads::Height);
		// Log press
		pads.press(padX, padY, !nextPressIsRelease);
		// Main grid press
		if (padX < MainGrid::Width) {
			handleMainGridPress(padX, padY, !nextPressIsRelease);
		}
		// Sidebar press
		else {
			handleSidebarPress(padX - 16, padY, !nextPressIsRelease);
		}
		// Reset release flag
		nextPressIsRelease = false;
		return;
	}
	// Encoders
	elementIndex = Encoder::indexFromPicIndex(picIndex);
	if (elementIndex != 255) {
		// Log press
		getEncoder(elementIndex).press(!nextPressIsRelease);
		// Handle press
		handleEncoderPress(elementIndex, !nextPressIsRelease);
		// Reset release flag
		nextPressIsRelease = false;
		return;
	}
	// Buttons
	elementIndex = Button::indexFromPicIndex(picIndex);
	if (elementIndex != 255) {
		// Log press
		getButton(elementIndex).press(!nextPressIsRelease);
		// Handle press
		handleButtonPress(elementIndex, !nextPressIsRelease);
		// Reset release flag
		nextPressIsRelease = false;
		return;
	}
	// We should not arrive here
	D_PRINT("Warning: unexpected pic index in UI: %d", picIndex);
}

void UI::readEncoderRotations() {
	for (auto& enc : encoderArray) {
		enc->readRotation();
	}
}

void UI::processEncoderRotations() {
	bool clockwise;
	for (uint8_t i = 0; i < Encoder::NumEncoders; i++) {
		if (getEncoder(i).processRotation(clockwise)) {
			handleEncoderRotation(i, clockwise);
		}
	}
}

} // namespace bareluge
