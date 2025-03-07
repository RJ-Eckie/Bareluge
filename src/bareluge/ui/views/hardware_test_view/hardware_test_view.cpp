#include "hardware_test_view.h"

namespace bareluge {

// Called when view is created
void HardwareTestView::init() {
	// Background-fill main grid
	for (uint8_t i = 0; i < mainGrid.getSize(); i++) {
		mainGrid.setColor(mainGrid.toX(i), mainGrid.toY(i), RGB::fromHue(lerp(120, 191, i, mainGrid.getSize())).dim(6));
	}
	// Background-fill sidebar
	for (uint8_t i = 0; i < sidebar.getSize(); i++) {
		sidebar.setColor(sidebar.toX(i), sidebar.toY(i), RGB::fromHue(lerp(24, 96, i, sidebar.getSize())).dim(6));
	}
	// Set bar leds for gold encoders
	getGoldEncoder(true).setLedBarValue(goldEncoderValue[true], ledBarUnipolar[true]);
	getGoldEncoder(false).setLedBarValue(goldEncoderValue[false], ledBarUnipolar[false]);
}

// Called right before oled is rendered
void HardwareTestView::drawOled() {
	static constexpr uint8_t cornerWidth = 6;
	oled.clear();
	// Corners
	oled.drawHorLine(0, 0, cornerWidth);
	oled.drawHorLine(0, oled.Width - 1 - cornerWidth, cornerWidth);
	oled.drawHorLine(oled.Height - 1, 0, cornerWidth);
	oled.drawHorLine(oled.Height - 1, oled.Width - 1 - cornerWidth, cornerWidth);
	oled.drawVerLine(0, 0, cornerWidth);
	oled.drawVerLine(0, oled.Height - 1 - cornerWidth, cornerWidth);
	oled.drawVerLine(oled.Width - 1, 0, cornerWidth);
	oled.drawVerLine(oled.Width - 1, oled.Height - 1 - cornerWidth, cornerWidth);
	// Crosshair
	oled.drawHorLine(oledLineY);
	oled.drawVerLine(oledLineX);
	// Square, constrained by oled dimensions
	int8_t leftX = clamp(oledLineX - oledBoxSize / 2, 0, max(oled.Width - oledBoxSize, 0));
	int8_t topY = clamp(oledLineY - oledBoxSize / 2, 0, max(oled.Height - oledBoxSize, 0));
	uint8_t width = min(oledBoxSize + 1, oled.Width - leftX);
	uint8_t height = min(oledBoxSize + 1, oled.Height - topY);
	oled.drawRect(leftX, topY, width, height);
}

// Called on button press or release
void HardwareTestView::handleButtonPress(uint8_t index, bool pressed) {
	D_PRINT("%s button %d", pressed ? "Pressed" : "Released", index);
	// Pressing a button toggles its led
	if (pressed) {
		buttonLit[index] = !buttonLit[index];
		getButton(index).setLed(buttonLit[index]);
		// If this is one of the mod buttons
		if (index <= to_underlying(ButtonName::Custom2Custom3)) {
			// Change modButtonsBinaryValue accordingly
			modButtonsBinaryValue = (modButtonsBinaryValue & ~(1 << (7 - index))) | (buttonLit[index] << (7 - index));
		}
	}
}

// Called on encoder press or release
void HardwareTestView::handleEncoderPress(uint8_t index, bool pressed) {
	D_PRINT("%s encoder %d", pressed ? "Pressed" : "Released", index);
	switch (static_cast<EncoderName>(index)) {
	case EncoderName::Vertical:
		oledLineY = DefaultOledLineY;
		break;
	case EncoderName::Horizontal:
		oledLineX = DefaultOledLineX;
		break;
	case EncoderName::GoldBottom:
	case EncoderName::GoldTop:
		if (pressed) {
			// Which of the gold encoders was pressed?
			bool isTopGoldEncoder = (index == to_underlying(EncoderName::GoldTop));
			// Toggle its unipolar/bipolar
			ledBarUnipolar[isTopGoldEncoder] = !ledBarUnipolar[isTopGoldEncoder];
			// Update its bar led
			getGoldEncoder(isTopGoldEncoder)
			    .setLedBarValue(goldEncoderValue[isTopGoldEncoder], ledBarUnipolar[isTopGoldEncoder]);
		}
		break;
	case EncoderName::Select:
		// Reset modButtonsBinaryValue
		modButtonsBinaryValue = 0;
		// Clear button leds
		for (uint8_t i = 0; i < 8; i++) {
			getButton(i).setLed(false);
		}
		break;
	case EncoderName::Tempo:
		oledBoxSize = DefaultBoxSize;
		break;
	}
}

// Called on encoder rotation
void HardwareTestView::handleEncoderRotation(uint8_t index, bool clockwise) {
	D_PRINT("Encoder %d rotated %s", index, clockwise ? "clockwise" : "counter-clockwise");
	switch (static_cast<EncoderName>(index)) {
	case EncoderName::Vertical:
		oledLineY = clamp(oledLineY + (clockwise ? 1 : -1), 0, oled.Height - 1);
		break;
	case EncoderName::Horizontal:
		oledLineX = clamp(oledLineX + (clockwise ? 1 : -1), 0, oled.Width - 1);
		break;
	case EncoderName::GoldBottom:
	case EncoderName::GoldTop: {
		// Which of the gold encoders was rotated?
		bool isTopGoldEncoder = (index == to_underlying(EncoderName::GoldTop));
		// Optional delay on the notch of bipolar bar led
		if (goldEncoderNotchDelay[isTopGoldEncoder]) {
			goldEncoderNotchDelay[isTopGoldEncoder]--;
			// Exit
			return;
		}
		// Adjust encoder value
		goldEncoderValue[isTopGoldEncoder] = clamp(goldEncoderValue[isTopGoldEncoder] + (clockwise ? 1 : -1), 0, 255);
		// Did we reach the center of a bipolar led bar?
		if (!ledBarUnipolar[isTopGoldEncoder] && goldEncoderValue[isTopGoldEncoder] == 128) {
			// Set a delay of a few frames
			goldEncoderNotchDelay[isTopGoldEncoder] = NotchDelayFrames;
		}
		// Update bar led
		getGoldEncoder(isTopGoldEncoder)
		    .setLedBarValue(goldEncoderValue[isTopGoldEncoder], ledBarUnipolar[isTopGoldEncoder]);
		break;
	}
	case EncoderName::Select:
		// Adjust value according to rotation
		modButtonsBinaryValue = clamp(modButtonsBinaryValue + (clockwise ? 1 : -1), 0, 255);
		// Map the bits of modButtonsBinaryValue to the mod button leds
		for (uint8_t buttonId = 0; buttonId < 8; buttonId++) {
			buttonLit[buttonId] = modButtonsBinaryValue & (1 << (7 - buttonId));
			getButton(buttonId).setLed(buttonLit[buttonId]);
		}
		break;
	case EncoderName::Tempo:
		oledBoxSize = clamp(oledBoxSize + (clockwise ? 2 : -2), 4, oled.Width);
		break;
	}
}

// Called on pad press or release in main grid
void HardwareTestView::handleMainGridPress(uint8_t x, uint8_t y, bool pressed) {
	D_PRINT("%s pad (%d, %d) on main grid", pressed ? "Pressed" : "Released", x, y);
	if (pressed) {
		uint8_t padIndex = mainGrid.toIndex(x, y);
		// Toggle pad status
		mainGridPadLit[padIndex] = !mainGridPadLit[padIndex];
		// Draw pad
		mainGrid.setColor(
		    x, y, RGB::fromHue(lerp(120, 191, padIndex, mainGrid.getSize())).dim(mainGridPadLit[padIndex] ? 0 : 6));
	}
}

// Called on pad press or release in sidebar
void HardwareTestView::handleSidebarPress(bool isRightColumn, uint8_t y, bool pressed) {
	D_PRINT("%s pad %d in the %s column of the sidebar", pressed ? "Pressed" : "Released", y,
	        isRightColumn ? "right" : "left");
	if (pressed) {
		uint8_t padIndex = sidebar.toIndex(isRightColumn, y);
		// Toggle pad status
		sidebarPadLit[padIndex] = !sidebarPadLit[padIndex];
		// Draw pad
		sidebar.setColor(isRightColumn, y,
		                 RGB::fromHue(lerp(24, 96, isRightColumn ? 15 - y : y, sidebar.getSize()))
		                     .dim(sidebarPadLit[padIndex] ? 0 : 6));
	}
}

} // namespace bareluge
