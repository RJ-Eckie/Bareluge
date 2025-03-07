#pragma once

// View includes - should always match ViewType in view.h

#include "hardware_test_view/hardware_test_view.h"
#include "td01_view/td01_view.h"
#include "tetris_view/tetris_view.h"
#include "view_selector_view/view_selector_view.h"

namespace bareluge {

// View creator class - should always match ViewType in view.h

class ViewCreator {
private:
	// Only accessible from ViewUI
	friend class ViewUI;

	// Creates a View object from a ViewType
	static constexpr View* createView(ViewType viewType, UiElements& uiElements) {
		switch (viewType) {
		case ViewType::ViewSelector:
			return new ViewSelectorView(uiElements);
		case ViewType::HardwareTest:
			return new HardwareTestView(uiElements);
		case ViewType::TechDemo01:
			return new TD01::TD01View(uiElements);
		case ViewType::Tetris:
			return new TetrisView(uiElements);
		// Add new view types above this line
		case ViewType::Base:
			return new View(ViewType::Base, uiElements);
		default:
			D_PRINT("Error: view type not implemented in view creator");
			return nullptr;
		}
	}
};

} // namespace bareluge
