#pragma once
#include "hardware/hardware_includes.h"

namespace bareluge {

// Use this to easily pass references to all ui elements between uis/views

struct UiElements {
	// References to all elements

	Oled& oled;
	std::array<Button*, Button::NumButtons>& buttonArray;
	std::array<Encoder*, Encoder::NumEncoders>& encoderArray;
	MainGrid& mainGrid;
	Sidebar& sidebar;

	// Get buttons and encoders from either their index or their name

	Button& getButton(uint8_t buttonIndex) const { return *buttonArray[buttonIndex]; }
	Button& getButton(ButtonName buttonName) const { return getButton(to_underlying(buttonName)); }
	Encoder& getEncoder(uint8_t encoderIndex) const { return *encoderArray[encoderIndex]; }
	Encoder& getEncoder(EncoderName encoderName) const { return getEncoder(to_underlying(encoderName)); }
	GoldEncoder& getGoldEncoder(bool isTopGoldEncoder) const {
		return *static_cast<GoldEncoder*>(encoderArray[isTopGoldEncoder ? to_underlying(EncoderName::GoldTop)
		                                                                : to_underlying(EncoderName::GoldBottom)]);
	}

	// Retrieve a range of buttons or encoders

	template <uint8_t N>
	void getButtonRange(std::array<Button*, N>& result, uint8_t startIndex = 0) {
		if (startIndex + N > Button::NumButtons) {
			D_PRINT("Warning: button range out of bounds");
		}
		for (uint8_t i = 0; i < N; ++i) {
			uint8_t index = startIndex + i;
			result[i] = (index < Button::NumButtons) ? buttonArray[index] : nullptr;
		}
	}

	template <uint8_t N>
	void getEncoderRange(std::array<Encoder*, N>& result, uint8_t startIndex = 0) {
		if (startIndex + N > Encoder::NumEncoders) {
			D_PRINT("Warning: encoder range out of bounds");
		}
		for (uint8_t i = 0; i < N; ++i) {
			uint8_t index = startIndex + i;
			result[i] = (index < Encoder::NumEncoders) ? encoderArray[index] : nullptr;
		}
	}

	// Clear all ui elements
	void clear() {
		oled.clear();
		for (auto& btn : buttonArray) {
			btn->setLed(false);
		}
		getGoldEncoder(true).clearLedBar();
		getGoldEncoder(false).clearLedBar();
		mainGrid.clear();
		sidebar.clear();
	}
};

} // namespace bareluge
