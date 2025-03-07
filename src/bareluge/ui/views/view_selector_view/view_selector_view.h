#pragma once
#include "ui/views/view.h"

namespace bareluge {

// Default view of ViewLauncherUI, which is the default UI for Bareluge
//
// - Every colored pad represents an implemented view
// - Pressing a colored pad selects the view
// - ViewLauncherUI uses getSelectedViewType() to retrieve the selected view type

class ViewSelectorView : public View {
public:
	// Constructor
	ViewSelectorView(UiElements& uiElements) : View(ViewType::ViewSelector, uiElements) {}

	// Initializer
	void init();

	// Allows UI to retrieve the selected view
	ViewType getSelectedViewType();

	// Called every frame
	void handleRepeatingActions() override;

	// Called right before pads are rendered
	void drawPads() override;

	// Called on pad press or release in main grid
	void handleMainGridPress(uint8_t x, uint8_t y, bool pressed) override;

private:
	// Constant
	static constexpr uint8_t NumViewColors = 12;
	static constexpr uint8_t ViewTypeStartIndex = to_underlying(ViewType::HardwareTest);
	static constexpr uint8_t ViewTypeEndIndex = to_underlying(ViewType::NumViewTypes);
	static constexpr uint8_t NumSelectableViewTypes = ViewTypeEndIndex - ViewTypeStartIndex;
	static constexpr uint8_t InvalidViewTypeIndex = to_underlying(ViewType::NumViewTypes) - ViewTypeStartIndex;

	// Properties
	uint8_t selectedViewTypeIndex{InvalidViewTypeIndex};
	uint32_t playButtonLastToggled{};

	// Launch animation
	static constexpr uint32_t launchDelay = TK::toSamples(250);
	static constexpr uint32_t spawnDelay = TK::toSamples(30);
	bool launchAnimationFinished{};
	uint32_t launchAnimationStartTime{};
	uint32_t lastPadSpawnTime{};
	uint8_t nextSpawnPadId{};

	void initLaunchAnimation();
	void handleLaunchAnimation();

	// Helper
	bool validViewSelected();
	RGB getViewTypeColor(uint8_t i);
};

} // namespace bareluge
