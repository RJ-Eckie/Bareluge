#pragma once
#include "ui/view_ui.h"

namespace bareluge {

// Default UI of Bareluge
//
// - Loads ViewSelectorView by default
// 	- The Play button blinks when ViewSelectorView has a valid view type selected
// 	- Pressing the Play button will launch the selected view type
// - Long-pressing the Back/Undo button will always return to the ViewSelectorView

class ViewLauncherUI : public ViewUI {
public:
	// Constructor
	ViewLauncherUI(PicHandler& picHandler, SpiHandler& spiHandler) : ViewUI(picHandler, spiHandler) {}

	// Initializer
	void init() override;

	// Called every frame
	void handleRepeatingActions() override;

	// Called on button press or release
	void handleButtonPress(uint8_t index, bool pressed) override;
};

} // namespace bareluge
