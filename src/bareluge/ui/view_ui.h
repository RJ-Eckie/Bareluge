#pragma once
#include "ui/ui.h"
#include "views/view.h"
#include "views/view_list.h"

namespace bareluge {

// ViewUI adds the handling of views to UI

class ViewUI : public UI {
public:
	// Constructor
	ViewUI(PicHandler& picHandler, SpiHandler& spiHandler)
	    : UI(picHandler, spiHandler), view(new View(ViewType::Base, uiElements)) {}

protected:
	// Properties
	View* view;

	// Launch View
	void launchView(ViewType viewType) {
		// Check type validity
		if (viewType >= ViewType::NumViewTypes) {
			D_PRINT("Error: invalid view type, launching view failed");
			return;
		}
		View* newView = ViewCreator::createView(viewType, uiElements); // Create new view
		if (!newView) {
			D_PRINT("Error: no view created, launching view failed");
			return;
		}
		view->deinit(); // Give current view a chance to deinitalize
		delete view;    // Delete current view
		clearUI();      // Force-clear the entire ui
		view = newView; // Make newView current
		view->init();   // Initialize the new current view
	}

	// == All UI behavior is forwareded to the active view by default == //

	// Called every frame
	void handleRepeatingActions() override { view->handleRepeatingActions(); }

	// Called right before oled is rendered
	void drawOled() override { view->drawOled(); }
	// Called right before pads are rendered
	void drawPads() override { view->drawPads(); }

	// Called on button press or release
	void handleButtonPress(uint8_t index, bool pressed) override { view->handleButtonPress(index, pressed); }
	// Called on encoder press or release
	void handleEncoderPress(uint8_t index, bool pressed) override { view->handleEncoderPress(index, pressed); }
	// Called on encoder rotation
	void handleEncoderRotation(uint8_t index, bool clockwise) override {
		view->handleEncoderRotation(index, clockwise);
	}
	// Called on pad press or release in main grid
	void handleMainGridPress(uint8_t x, uint8_t y, bool pressed) override { view->handleMainGridPress(x, y, pressed); }
	// Called on pad press or release in sidebar
	void handleSidebarPress(bool isRightColumn, uint8_t y, bool pressed) override {
		view->handleSidebarPress(isRightColumn, y, pressed);
	}
};

} // namespace bareluge
