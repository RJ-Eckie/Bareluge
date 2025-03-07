#pragma once
#include "hardware/hardware_includes.h"
#include "ui/ui_elements.h"

namespace bareluge {

// View types - these should always match view_list.h
enum class ViewType : uint8_t {
	ViewSelector,
	HardwareTest,
	TechDemo01,
	Tetris,
	// Add new view types above this line
	NumViewTypes,
	Base // just a View()
};

class View {
public:
	// Constructor
	View(ViewType type, UiElements& uiElements)
	    : oled(uiElements.oled), mainGrid(uiElements.mainGrid), sidebar(uiElements.sidebar), type(type),
	      uiElements(uiElements) {}

	// Properties

	ViewType getType() { return type; }

	// Called when view is created
	virtual void init() {}
	// Called right before view is destroyed
	virtual void deinit() {}

	// == View behavior == //

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

protected:
	// UI Elements

	Oled& oled;
	MainGrid& mainGrid;
	Sidebar& sidebar;
	UiElements& uiElements;

	// Get buttons and encoders from either their index or their name

	Button& getButton(uint8_t buttonIndex) const { return uiElements.getButton(buttonIndex); }
	Button& getButton(ButtonName buttonName) const { return uiElements.getButton(buttonName); }
	Encoder& getEncoder(uint8_t encoderIndex) const { return uiElements.getEncoder(encoderIndex); }
	Encoder& getEncoder(EncoderName encoderName) const { return uiElements.getEncoder(encoderName); }
	GoldEncoder& getGoldEncoder(bool isTopGoldEncoder) const { return uiElements.getGoldEncoder(isTopGoldEncoder); }

	// Clear the entire view
	void clearView() { uiElements.clear(); }

private:
	// Properties
	ViewType type;
};

} // namespace bareluge
