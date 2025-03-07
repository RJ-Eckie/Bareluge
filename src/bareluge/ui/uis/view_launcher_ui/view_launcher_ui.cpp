#include "view_launcher_ui.h"

namespace bareluge {

// Initializer
void ViewLauncherUI::init() {
	// Launch the selector view
	launchView(ViewType::ViewSelector);
}

// Called every frame
void ViewLauncherUI::handleRepeatingActions() {
	// First allow the active view to execute its main frame
	ViewUI::handleRepeatingActions();
	// Holding Back button for 500ms brings you back to selector view
	if (view->getType() != ViewType::ViewSelector
	    && getButton(ButtonName::BackUndoRedo).getHoldTime() >= TK::toSamples(500)) {
		// Back button can not be used for another hold-based action
		getButton(ButtonName::BackUndoRedo).disableHold();
		launchView(ViewType::ViewSelector);
	}
}

// Called on button press or release
void ViewLauncherUI::handleButtonPress(uint8_t index, bool pressed) {
	switch (static_cast<ButtonName>(index)) {
	case ButtonName::BackUndoRedo:
		// The back button led reflects pressed status
		getButton(ButtonName::BackUndoRedo).setLed(pressed);
		// Exiting the method here prohibits any loaded view from using the back button
		return;
		break;
	case ButtonName::Play:
		// In the view selector
		if (view->getType() == ViewType::ViewSelector) {
			// When releasing
			if (!pressed) {
				// Retrieve the selected view type
				ViewType selectedViewType = static_cast<ViewSelectorView*>(view)->getSelectedViewType();
				// If a valid view is selected
				if (selectedViewType < ViewType::NumViewTypes) {
					// Launch view
					launchView(selectedViewType);
					return;
				}
			}
		}
		break;
	}
	// All unhandled button presses: process as usual (will be forwareded to active view)
	ViewUI::handleButtonPress(index, pressed);
}

} // namespace bareluge
