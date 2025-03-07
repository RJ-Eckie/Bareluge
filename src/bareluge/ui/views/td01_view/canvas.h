#pragma once
#include "canvas_viewer.h"
#include "ui/ui_elements.h"

namespace bareluge::TD01 {

// The Canvas holds all drawn pad colors, manages moving the visble selection over the canvas, setting and erasing
// colors inside the visible selection and drawing the colors inside the visible selection to the pads of the main grid.
// It also holds a CanvasViewer, which draws a representation of the canvas to the oled display

class Canvas {
private:
	// The dimensions are based on the oled display
	static constexpr uint8_t Width = CanvasViewer::PadWidth;
	static constexpr uint8_t Height = CanvasViewer::PadHeight;
	// How much can this selection move within the canvas borders?
	static constexpr uint8_t MaxXOffset = Width - MainGrid::Width;
	static constexpr uint8_t MaxYOffset = Height - MainGrid::Height;

public:
	Canvas(UiElements& uiElements)
	    : mainGrid(uiElements.mainGrid), eraserButton(uiElements.getButton(ButtonName::SaveDelete)), padColor(),
	      canvasViewer(uiElements.oled), xOffset(MaxXOffset / 2), yOffset(MaxYOffset / 2), eraserOn(),
	      eraserLightLastToggled(), fullPadsRedrawRequested(true), fullOledRedrawRequested(true) {}

	void handleRepeatingActions() {
		// Handle eraser mode blinking light
		if (eraserOn && TK::doEvery(TK::toSamples(500), eraserLightLastToggled)) {
			eraserButton.toggleLed();
		}
	}

	void clear() {
		padColor = {};
		setEraser(false);
		setDefaultXOffset();
		setDefaultYOffset();
		requestFullRedraw();
	}

	void drawOled() {
		if (fullOledRedrawRequested) {
			canvasViewer.redraw(padColor, xOffset, yOffset);
			fullOledRedrawRequested = false; // We fulfilled this request
		}
	}

	void drawPads() {
		if (fullPadsRedrawRequested) {
			redrawPads();
			fullPadsRedrawRequested = false; // We fulfilled this request
		}
	}

	void changeXOffset(bool clockwise) {
		xOffset = clamp(xOffset + (clockwise ? 1 : -1), 0, MaxXOffset);
		requestFullRedraw();
	}
	void setDefaultXOffset() {
		xOffset = MaxXOffset / 2;
		requestFullRedraw();
	}
	void changeYOffset(bool clockwise) {
		yOffset = clamp(yOffset + (clockwise ? 1 : -1), 0, MaxYOffset);
		requestFullRedraw();
	}
	void setDefaultYOffset() {
		yOffset = MaxYOffset / 2;
		requestFullRedraw();
	}

	void setColor(uint8_t x, uint8_t y, RGB newColor) {
		// Pressing a pad that already has this color
		if (rgbEquals(newColor, padColor[x + xOffset][y + yOffset])) {
			// turns it off
			padColor[x + xOffset][y + yOffset] = RGB{};
		}
		else {
			// Set color
			padColor[x + xOffset][y + yOffset] = newColor;
		}
		// Update main grid
		mainGrid.setColor(x, y, padColor[x + xOffset][y + yOffset]);
		// Update oled viewer
		canvasViewer.setColor(x + xOffset, y + yOffset, padColor[x + xOffset][y + yOffset]);
	}

	void handleMainGridPress(uint8_t x, uint8_t y, bool pressed, RGB activeColor) {
		if (pressed) {
			setColor(x, y, eraserOn ? RGB{} : activeColor);
		}
	}

	// Handling the eraser

	bool eraserActive() { return eraserOn; }

	void setEraser(bool on) {
		eraserOn = on;
		// Turn off the button led when eraser is turned off
		if (!on) {
			eraserButton.setLed(false);
		}
	}

	void toggleEraser() { setEraser(!eraserOn); }

private:
	// UI Elements
	MainGrid& mainGrid;
	Button& eraserButton;

	// Properties
	std::array<std::array<RGB, Height>, Width> padColor; // Colors on the canvas
	CanvasViewer canvasViewer;                           // Handler for viewing the canvas on the oled
	uint8_t xOffset;                                     // Offset of the editable selection from the left border
	uint8_t yOffset;                                     // Offset of the editable selection from the top border
	bool eraserOn;                                       // Is the eraser active?
	uint32_t eraserLightLastToggled;                     // To manage blinking the eraser button
	bool fullPadsRedrawRequested;                        // Should we redraw the visible grid?
	bool fullOledRedrawRequested;                        // Should we redraw the oled?

	// Call this to redraw the whole grid and oled next time they are rendered
	void requestFullRedraw() {
		fullPadsRedrawRequested = true;
		fullOledRedrawRequested = true;
	}

	// Redraw the whole grid
	void redrawPads() {
		// Draw all pads currently visible on the main grid
		for (uint8_t x = 0; x < MainGrid::Width; x++) {
			for (uint8_t y = 0; y < MainGrid::Height; y++) {
				mainGrid.setColor(x, y, padColor[x + xOffset][y + yOffset]);
			}
		}
	}

	// Helper
	static constexpr bool rgbEquals(RGB rgb1, RGB rgb2) {
		return (rgb1.r | (rgb1.g << 8) | (rgb1.b << 16)) == (rgb2.r | (rgb2.g << 8) | (rgb2.b << 16));
	}
};

} // namespace bareluge::TD01
