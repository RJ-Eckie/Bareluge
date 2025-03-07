#include "td01_view.h"

namespace bareluge::TD01 {

// Called when view is created
void TD01View::init() {
	clearView();
	colorPicker.init();
}

// Called every frame
void TD01View::handleRepeatingActions() {
	// Handle new canvas animation
	if (loadingNewCanvas && (TK::doEvery(TK::toSamples(100), newCanvasLog))) {
		// Slowly fill button leds Synth through CV
		for (uint8_t i = 0; i < 4; i++) {
			getButton(to_underlying(ButtonName::Synth) + i)
			    .setLed(getButton(ButtonName::LoadNew).getHoldTime() >= i * LoadNewCanvasTime / 4);
		}
		// Waited long enough
		if (getButton(ButtonName::LoadNew).getHoldTime() >= LoadNewCanvasTime) {
			endLoadingNewCanvas();
			// Create new canvas
			canvas.clear();
		}
	}
	// Throw to canvas
	canvas.handleRepeatingActions();
}

// Called right before oled is rendered
void TD01View::drawOled() {
	canvas.drawOled(); // Drawing is handled by the canvas
}

// Called right before pads are rendered
void TD01View::drawPads() {
	canvas.drawPads(); // Drawing is handled by the canvas
}

// Called on button press or release
void TD01View::handleButtonPress(uint8_t index, bool pressed) {
	switch (static_cast<ButtonName>(index)) {
	case ButtonName::SaveDelete:
		// Shift + SaveDelete toggles the eraser
		if (pressed && getButton(ButtonName::Shift).isPressed()) {
			canvas.toggleEraser();
		}
		break;
	case ButtonName::LoadNew:
		// Start new canvas loading animation on Shift + LoadNew
		if (pressed && getButton(ButtonName::Shift).isPressed()) {
			startLoadingNewCanvas();
		}
		// Stop new canvas loading animation on release of LoadNew button
		if (!pressed) {
			endLoadingNewCanvas();
		}
		break;
	case ButtonName::Shift:
		// Stop new canvas loading animation on release of Shift button
		endLoadingNewCanvas();
		break;
	}
}

// Called on encoder press or release
void TD01View::handleEncoderPress(uint8_t index, bool pressed) {
	// These all set parameters back to default values
	if (pressed) {
		switch (static_cast<EncoderName>(index)) {
		case EncoderName::Vertical:
			canvas.setDefaultYOffset();
			break;
		case EncoderName::Horizontal:
			canvas.setDefaultXOffset();
			break;
		case EncoderName::GoldBottom:
			colorPicker.setDefaultSaturation();
			break;
		case EncoderName::GoldTop:
			colorPicker.setDefaultValue();
			break;
		case EncoderName::Select:
			colorPicker.setDefaultHueOffset();
			break;
		}
	}
}

// Called on encoder rotation
void TD01View::handleEncoderRotation(uint8_t index, bool clockwise) {
	switch (static_cast<EncoderName>(index)) {
	case EncoderName::Vertical:
		// This feels more natural with clockwise inverted
		canvas.changeYOffset(!clockwise);
		break;
	case EncoderName::Horizontal:
		canvas.changeXOffset(clockwise);
		break;
	case EncoderName::GoldBottom:
		colorPicker.changeActiveSaturation(clockwise);
		break;
	case EncoderName::GoldTop:
		colorPicker.changeActiveValue(clockwise);
		break;
	case EncoderName::Select:
		colorPicker.changeHueOffset(clockwise);
		break;
	}
}

// Called on pad press or release in main grid
void TD01View::handleMainGridPress(uint8_t x, uint8_t y, bool pressed) {
	canvas.handleMainGridPress(x, y, pressed, colorPicker.getActiveColor());
}

// Called on pad press or release in sidebar
void TD01View::handleSidebarPress(bool isRightColumn, uint8_t y, bool pressed) {
	colorPicker.handleSidebarPress(isRightColumn, y, pressed);
}

void TD01View::startLoadingNewCanvas() {
	loadingNewCanvas = true;
	getButton(ButtonName::LoadNew).setLed(true);
}

void TD01View::endLoadingNewCanvas() {
	if (loadingNewCanvas) {
		getButton(ButtonName::LoadNew).setLed(false);
		for (uint8_t i = 0; i < 4; i++) {
			getButton(to_underlying(ButtonName::Synth) + i).setLed(false);
		}
		loadingNewCanvas = false;
	}
}

} // namespace bareluge::TD01
