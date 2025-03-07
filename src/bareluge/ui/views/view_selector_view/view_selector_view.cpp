#include "view_selector_view.h"

namespace bareluge {

void ViewSelectorView::init() {
	initLaunchAnimation();
}

void ViewSelectorView::handleRepeatingActions() {
	// Blink the play button if a valid view is selected
	if (validViewSelected()) {
		if (TK::doEvery(TK::toSamples(500), playButtonLastToggled)) {
			getButton(ButtonName::Play).toggleLed();
		}
	}
}

void ViewSelectorView::drawPads() {
	// Lil animation when we launch the view selector
	if (!launchAnimationFinished) {
		handleLaunchAnimation();
		return;
	}
	// When not in animation: redraw pads
	for (uint8_t i = 0; i < NumSelectableViewTypes; i++) {
		// The y value is flipped so we start top-left
		mainGrid.setColor(mainGrid.toX(i), 7 - mainGrid.toY(i), getViewTypeColor(i));
	}
}

void ViewSelectorView::handleMainGridPress(uint8_t x, uint8_t y, bool pressed) {
	// We only care about presses
	if (pressed) {
		// Get the linear index of the press (y value is flipped)
		uint8_t pressedId = mainGrid.toIndex(x, 7 - y);
		// Pressed already selected view type?
		if (pressedId == selectedViewTypeIndex) {
			// De-select
			selectedViewTypeIndex = InvalidViewTypeIndex;
			// Turn off play button
			getButton(ButtonName::Play).setLed(false);
		}
		// Pressed valid view type
		else if (pressedId < NumSelectableViewTypes) {
			// Select
			selectedViewTypeIndex = pressedId;
		}
		// Pressed invalid pad
		else {
			// De-select
			selectedViewTypeIndex = InvalidViewTypeIndex;
			// Turn off play button
			getButton(ButtonName::Play).setLed(false);
		}
	}
}

ViewType ViewSelectorView::getSelectedViewType() {
	// Cast selected index to appropriate view type
	return validViewSelected() ? static_cast<ViewType>(selectedViewTypeIndex + ViewTypeStartIndex)
	                           : ViewType::NumViewTypes;
}

bool ViewSelectorView::validViewSelected() {
	return selectedViewTypeIndex < InvalidViewTypeIndex;
}

RGB ViewSelectorView::getViewTypeColor(uint8_t i) {
	// The selected view is highlighted
	return RGB::fromHuePastel(2 * (i % NumViewColors) * (256 / NumViewColors)).dim(i != selectedViewTypeIndex ? 2 : 0);
}

void ViewSelectorView::initLaunchAnimation() {
	nextSpawnPadId = 0;
	launchAnimationFinished = false;
	clearView();
	TK::log(launchAnimationStartTime);
}

void ViewSelectorView::handleLaunchAnimation() {
	// Short wait on blank screen
	if (!TK::hasPassed(launchDelay, launchAnimationStartTime)) {
		return;
	}
	// Short delay between pads
	if (TK::doEvery(spawnDelay, lastPadSpawnTime)) {
		// Spawn a pad
		mainGrid.setColor(mainGrid.toX(nextSpawnPadId), 7 - mainGrid.toY(nextSpawnPadId),
		                  getViewTypeColor(nextSpawnPadId));
		// Move to next pad
		nextSpawnPadId++;
		// Check for end of animation
		if (nextSpawnPadId == NumSelectableViewTypes) {
			launchAnimationFinished = true;
		}
	}
}

} // namespace bareluge
