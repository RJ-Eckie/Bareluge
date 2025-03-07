#pragma once
#include "hardware/handlers/pic_handler.h"
#include "utilities/pressable.h"

namespace bareluge {

// Circular button with binary led

enum class ButtonName : uint8_t {
	// Mod buttons
	LevelPan,
	CutoffResonance,
	AttackRelease,
	DelayTimeDelayAmount,
	SidechainReverb,
	ModRateModDepth,
	StutterCustom1,
	Custom2Custom3,
	// Project buttons
	AffectEntire,
	Song,
	Clip,
	Synth,
	Kit,
	Midi,
	CV,
	Keyboard,
	Scale,
	CrossScreen,
	// Global buttons
	BackUndoRedo,
	LoadNew,
	SaveDelete,
	LearnInput,
	TapTempo,
	SyncScaling,
	TripletsView,
	Play,
	RecordResample,
	Shift
};

class Button : public Pressable {
public:
	// Constant
	static constexpr uint8_t NumButtons = 28;

	// Static
	static constexpr uint8_t indexFromPicIndex(uint8_t picIndex) {
		// Try to find picIndex in the list
		auto findIndex = std::find(ButtonPicIndex.begin(), ButtonPicIndex.end(), picIndex);
		// Found a match
		if (findIndex != ButtonPicIndex.end()) {
			return std::distance(ButtonPicIndex.begin(), findIndex);
		}
		// Didn't find a match
		return 255;
	}

	// Constructor
	Button(uint8_t index, PicHandler& picHandler) : Pressable(), index(index), picHandler(picHandler) {}

	// Get Led
	bool getLedState() { return ledOn; }

	// Set Led
	void setLed(bool on) {
		ledOn = on;
		picHandler.send(on ? PicMessage::ButtonLedOn : PicMessage::ButtonLedOff, LedPicIndex[index]);
	}

	// Toggle led
	void toggleLed() { setLed(!getLedState()); }

private:
	// Constant
	static constexpr std::array<uint8_t, NumButtons> ButtonPicIndex = {145, 154, 163, 172, 146, 155, 164, 173, 147, 156,
	                                                                   165, 149, 158, 167, 176, 174, 150, 168, 160, 159,
	                                                                   177, 151, 178, 169, 161, 179, 170, 152};
	static constexpr std::array<uint8_t, NumButtons> LedPicIndex = {
	    1, 10, 19, 28, 2, 11, 20, 29, 3, 12, 21, 5, 14, 23, 32, 30, 6, 24, 16, 15, 33, 7, 34, 25, 17, 35, 26, 8,
	};

	// Properties
	uint8_t index;
	PicHandler& picHandler;
	bool ledOn{};
};

} // namespace bareluge
