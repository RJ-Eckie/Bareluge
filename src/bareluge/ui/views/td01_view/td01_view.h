#pragma once
#include "canvas.h"
#include "color_picker.h"
#include "ui/views/view.h"

namespace bareluge::TD01 {

// Tech Demo 01
//
// UI:
// - The oled display shows a representation of your canvas, the rectangle shows the current selection
// - The main grid shows the colors of the current selection of your canvas
// - The top row of the sidebar shows two selected colors
// - The second row of the sidebar shows which of the selected colors is active
// - The remaining rows of the sidebar show the available hues, with the selected hue highlighted
// - The mod buttons (Level/Pan through Custom2/Custom3) show the fine-offset of the hue of the active color
// - The led bar of the Bottom Gold encoder shows the saturation of the active color
// - The led bar of the Top Gold encoder shows the value (brightness) of the active color

// Controls:
// - Rotate the Vertical and Horizontal encoders to change the canvas selection position
// - Press a main grid pad to give it the active color
// 		- If the pad already has the active color, it will be erased
// - Press shift + Save/Delete to toggle the eraser
// 		- While the eraser is active, any main grid press erases that pad
// - Press a pad in the top two rows of the sidebar to select which color is active
// - Press a pad in the lower six rows of the sidebar to select a hue for the active color
// - For the Bottom Gold, Top Gold and Select encoders:
// 		- Rotate to tweak respectively saturation, value and hue-offset of the active color
// 		- Press to reset to default value (75%, 75% and 0 respectively)
// - Hold shift + Load/New to start with a new, empty canvas

// Refer to readme.md for documentation on how this class was implemented

class TD01View : public View {
public:
	// Constructor
	TD01View(UiElements& uiElements)
	    : View(ViewType::TechDemo01, uiElements), canvas(uiElements), colorPicker(uiElements) {}

	// Called when view is created
	void init() override;
	// Called every frame
	void handleRepeatingActions() override;

	// Called right before oled is rendered
	void drawOled() override;
	// Called right before pads are rendered
	void drawPads() override;

	// Called on button press or release
	void handleButtonPress(uint8_t index, bool pressed) override;
	// Called on encoder press or release
	void handleEncoderPress(uint8_t index, bool pressed) override;
	// Called on encoder rotation
	void handleEncoderRotation(uint8_t index, bool clockwise) override;
	// Called on pad press or release in main grid
	void handleMainGridPress(uint8_t x, uint8_t y, bool pressed) override;
	// Called on pad press or release in sidebar
	void handleSidebarPress(bool isRightColumn, uint8_t y, bool pressed) override;

private:
	// Properties
	Canvas canvas;
	ColorPicker colorPicker;

	// Load new canvas
	static const uint32_t LoadNewCanvasTime = TK::toSamples(2000);
	bool loadingNewCanvas{};
	uint32_t newCanvasLog{};
	void startLoadingNewCanvas();
	void endLoadingNewCanvas();
};

} // namespace bareluge::TD01
