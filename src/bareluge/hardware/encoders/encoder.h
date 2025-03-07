#pragma once
#include "utilities/pressable.h"
#include "utilities/utilities.h"

// Low-level import
extern "C" {
#include "RZA1/gpio/gpio.h"
}

namespace bareluge {

// Pressable encoder base class

enum class EncoderName : uint8_t { Vertical, Horizontal, GoldBottom, GoldTop, Select, Tempo };

class Encoder : public Pressable {
public:
	// Constant
	static constexpr uint8_t NumEncoders = 6;

	// Static
	static constexpr uint8_t indexFromPicIndex(uint8_t picIndex) {
		for (uint8_t i = 0; i < Encoder::NumEncoders; i++) {
			if (picIndex == Encoder::PicIndex[i]) {
				return i;
			}
		}
		return 255;
	}

	// Constructor
	Encoder(uint8_t index) : Pressable(), index(index) {
		if (index >= NumEncoders) {
			D_PRINT("Error: Encoder index out of bounds");
		}
	};

	// Read rotation values
	void readRotation();
	// Process rotation values, returns whether rotation happened
	virtual bool processRotation(bool& clockwise);

protected:
	// Constant
	static constexpr std::array<uint8_t, NumEncoders> PicIndex = {144, 153, 162, 171, 175, 157};
	static constexpr std::array<std::array<uint8_t, 4>, NumEncoders> PinList = {
	    {{1, 8, 1, 10}, {1, 11, 1, 12}, {1, 0, 1, 15}, {1, 5, 1, 4}, {1, 2, 1, 3}, {1, 7, 1, 6}}};

	// Properties
	uint8_t index;
	std::array<bool, 4> pinState{}; // Respectively: Pin A state, Pin B state, Pin A new state, Pin B new state
	int8_t deltaRotation{};         // Amount of change since last processed
};

} // namespace bareluge
