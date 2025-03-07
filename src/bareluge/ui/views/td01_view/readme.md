# Tech Demo 01

Tech Demo 01 was written to provide an example of a robust implementation of a more complex view

- [Usage](#usage)
- [Implementation](#implementation)
  - [Namespace](#namespace)
  - [Class separation](#class-separation)
  - [Having separate classes manage separate UI elements](#having-separate-classes-manage-separate-ui-elements)
  - [Optimizing pad and oled drawing](#optimizing-pad-and-oled-drawing)

## Usage
Tech Demo 01 is a sort of bitmap editor. The full size of the canvas is 63 x 20 pixels, or as large as can fit on the oled display with each canvas pixel occupying four pixels on the oled display. The main grid is used to view and edit a 16x8 pixel selection of the canvas.

### UI
- The oled display shows a representation of your canvas, the rectangle shows the current selection
- The main grid shows the colors of the current selection of your canvas
- The top row of the sidebar shows two selected colors
- The second row of the sidebar shows which of the selected colors is active
- The remaining rows of the sidebar show the available hues, with the selected hue highlighted
- The mod buttons (Level/Pan through Custom2/Custom3) show the fine-offset of the hue of the active color
- The led bar of the Bottom Gold encoder shows the saturation of the active color
- The led bar of the Top Gold encoder shows the value (brightness) of the active color

### Controls
- Rotate the Vertical and Horizontal encoders to change the canvas selection position
- Press a main grid pad to give it the active color
		- If the pad already has the active color, it will be erased
- Press Shift + Save/Delete to toggle the eraser
		- While the eraser is active, any main grid press erases that pad
- Press a pad in the top two rows of the sidebar to select which color is active
- Press a pad in the lower six rows of the sidebar to select a hue for the active color
- For the Bottom Gold, Top Gold and Select encoders:
		- Rotate to tweak respectively saturation, value and hue-offset of the active color
		- Press to reset to default value (75%, 75% and 0 respectively)
- Hold Shift + Load/New to start with a new, empty canvas

## Implementation
### Namespace
This view is wrapped in its own `namespace TD01`. It is using some quite general terms such as `Canvas`. By wrapping the view in its own namespace, we avoid any naming conflicts with the Bareluge repo in the future.

The this namespace needs to be included when the view is added to Bareluge. Consider the difference between the view creation of `HardwareTestView` and `TD01View` in `view_list.h`:
```cpp
// Creates a view object from a ViewType
static constexpr View* createView(ViewType viewType, UiElements& uiElements) {
    switch (viewType) {
    case ViewType::HardwareTest:
	return new HardwareTestView(uiElements);
    case ViewType::TechDemo01:
	return new TD01::TD01View(uiElements); // The namespace TD01 is included in the type of the view
    }
}
```
### Class separation
The view is complex enough to warrant separating behavior into separate classes. This is the setup:
- `TD01View`

    The base class only really does two things:
    1. Reading out user input and calling the appropriate methods in `Canvas` and `ColorPicker`
    2. Loading a new canvas

    This keeps the base class quite easy to read
- `Canvas`

    This holds the state of the canvas, draws it to the main grid and implements all its behavior. It holds a `CanvasViewer`, which manages drawing the contents of the canvas to the oled display
- `ColorPicker`

    This manages the color picking workflow. Picking and tweaking a color is fully independent from drawing the color to the canvas, which makes it logical to separate its behavior. The resulting active color can be retrieved with public methods `RGB getActiveColor()` and `HSV getActiveHSV()`
- `HSV`

    A custom type to store HSV colors, as HSV isn't globally implemented yet
### Having separate classes manage separate UI elements
In complex implementations, it's important to concider which parts of your view will handle which UI elements. Tech Demo 01 implements a simple and robust way to handle this:
#### 1. Passing around UI elements
In any `View` or `UI` implementation you have access to the property `uiElements`. This property holds references to all UI elements and includes methods to retrieve a range of buttons or encoders from `buttonArray` and `encoderArray`. If you want to create a custom class that handles part of the UI, simply pass it uiElements in its constructor. You can find the implementation of `UiElements` [here](../../ui_elements.h)
#### 2. Saving only the needed UI elements
  Saving a copy of `uiElements` is the quickest, but also the least safe way to give your custom class access to the UI. A safer way is saving references to only the elements that your class needs. Let's look at a few examples:
  1. `Canvas` saves references to the main grid and to the Save/Delete button. This allows the canvas to blink the Save/Delete button when the eraser is active, without having access to any of the other buttons. Note that the Save/Delete button is saved in `Button& eraserButton`. Instead of using the name of the button itself for the variable, the purpose of the button is used. This makes the code easier to understand
  2. `Canvas` forwards `uiElements` to `CanvasViewer`, who in turn only saves a reference to the oled display
  3. A slightly more advanced usage exists in `ColorPicker`. Its constructor uses `uiElements.getButtonRange<8>(modButtons)` to save pointers to the first eight buttons to `std::array<Button*, 8> modButtons`

This might seem like a slightly overcomplicated way to save these references, but you may thank yourself in the future for putting in the extra effort. The big advantage of doing it like this is that you get to decide at the design-stage of your view which class gets access to which elements. If in the future a different developer (including future-you) comes back to this code to add or debug something, they can't accidentally address parts of the UI that weren't meant to be accessed by that class
### Optimizing pad and oled drawing
The oled and main grid do not always need to be fully redrawn. For example: when a color is set to one pixel of the canvas, both the main grid and the oled need to be updated for that one pixel - but that's it. Both `Canvas` and `CanvasViewer` have a method `setColor(x, y, newColor)` that takes care of that.

On the other hand, changing the position of the editable selection of the canvas leads to a much bigger change. In this case we opt to fully redraw the main grid and the oled display. Here's how that happens:
- `Canvas` calls `requestFullRedraw()`, which sets flags `fullPadsRedrawRequested` and `fullOledRedrawRequested`
- Just before the oled is about to be rendered, the `UI` calls `drawOled()`. When `fullOledRedrawRequested` is true, `drawOled()` asks `CanvasViewer` to fully redraw the oled and then sets `fullOledRedrawRequested` to false
- `drawPads()` does the same thing but for the main grid. We need to use separate flags for the oled and pads because they **are not** rendered at the same time

Drawing the sidebar is handled by `ColorPicker`. As these are only 16 pads, I didn't think optimization would amount to anything. So whenever anything changes in the color picker, we just redraw all 16 pads.

*Note about optimization: in an implementation of this size, there is absolutely no need to optimize anything. I'm just including it **as an example** how to use the Bareluge framework to write efficient code*
