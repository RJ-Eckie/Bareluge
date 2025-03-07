#pragma once
#include "hardware/handlers/pic_handler.h"
#include "hardware/handlers/spi_handler.h"
#include "hardware/hardware_includes.h"
#include "ui_elements.h"

namespace bareluge {

// The UI handles global operation of the user interface

class UI {
public:
	// Constructor
	UI(PicHandler& picHandler, SpiHandler& spiHandler) : oled(spiHandler), pads(picHandler) {
		// Create buttons
		for (uint8_t i = 0; i < Button::NumButtons; i++) {
			buttonArray[i] = new Button(i, picHandler);
		}
		// Create encoders
		for (uint8_t i = 0; i < Encoder::NumEncoders; i++) {
			switch (static_cast<EncoderName>(i)) {
			// Gold encoders
			case EncoderName::GoldBottom:
				encoderArray[i] = new GoldEncoder(picHandler, false);
				break;
			case EncoderName::GoldTop:
				encoderArray[i] = new GoldEncoder(picHandler, true);
				break;
			// Regular encoders
			default:
				encoderArray[i] = new DetentedEncoder(i);
				break;
			}
		}
	}

	// Only give Deluge access to the following methods
	class AccessKey {
	private:
		friend class Deluge;
		AccessKey() {}
	};

	// Base initializer
	void baseInit(AccessKey);
	// Base frame method
	void baseHandleRepeatingActions(AccessKey);
	// The pic sends us a message when a ui element is pressed
	void handlePicIndex(uint8_t picIndex, AccessKey);

protected:
	// Called when ui is created
	virtual void init() {}

	// == UI behavior == //

	// Called every frame
	virtual void handleRepeatingActions() {}
	// Called right before oled is rendered
	virtual void drawOled() {}
	// Called right before pads are rendered
	virtual void drawPads() {}

	// Called on button press or release
	virtual void handleButtonPress(uint8_t index, bool pressed) {}
	// Called on encoder press or release
	virtual void handleEncoderPress(uint8_t index, bool pressed) {}
	// Called on encoder rotation
	virtual void handleEncoderRotation(uint8_t index, bool clockwise) {}
	// Called on pad press or release in main grid
	virtual void handleMainGridPress(uint8_t x, uint8_t y, bool pressed) {}
	// Called on pad press or release in sidebar
	virtual void handleSidebarPress(bool isRightColumn, uint8_t y, bool pressed) {}

	// UI Elements
	Oled oled;
	MainGrid mainGrid{pads};
	Sidebar sidebar{pads};

	// Get buttons and encoders from either their index or their name

	Button& getButton(uint8_t buttonIndex) const { return uiElements.getButton(buttonIndex); }
	Button& getButton(ButtonName buttonName) const { return uiElements.getButton(buttonName); }
	Encoder& getEncoder(uint8_t encoderIndex) const { return uiElements.getEncoder(encoderIndex); }
	Encoder& getEncoder(EncoderName encoderName) const { return uiElements.getEncoder(encoderName); }
	GoldEncoder& getGoldEncoder(bool isTopGoldEncoder) const { return uiElements.getGoldEncoder(isTopGoldEncoder); }

	// Used to easily pass around references to all ui elements
	UiElements uiElements{oled, buttonArray, encoderArray, mainGrid, sidebar};

	// Clear the entire UI
	void clearUI() { uiElements.clear(); }

private:
	// Constant
	static constexpr uint8_t PadsFPS = 30; // Render pads at 30 fps
	static constexpr uint8_t OledFPS = 30; // Render oled at 30 fps

	// Raw UI Elements
	std::array<Button*, Button::NumButtons> buttonArray;
	std::array<Encoder*, Encoder::NumEncoders> encoderArray;
	Pads pads;

	// Properties
	bool nextPressIsRelease{};
	uint32_t lastPadsRenderTime{};
	uint32_t lastOledRenderTime{};

	// Handling encoders
	void readEncoderRotations();
	void processEncoderRotations();
};

} // namespace bareluge
