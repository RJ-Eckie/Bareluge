# Working with UIs and Views
UIs and views both implement the following virtual methods. The methods don't implement any behavior by default. Override them in your UI/view to implement custom behavior

| Method | Description |
|----|----|
| `init()` | Gets called when the UI/view is first loaded. Can be used to set up variables, draw the initial UI, etc |
| `deinit()` | Gets called right before the current view is destroyed to be replaced by another view. As uis can't be switched or destroyed, they don't implement this method |
| `handleRepeatingActions()` | This is a frame in the main loop. Any repeating behavior goes here. Note that code can *never* be blocking |
| `drawOled()` | Gets called right before the oled display is rendered |
| `drawPads()` | Gets called right before the rgb pads are rendered |
| `handleButtonPress()` | Gets called when a circular button is pressed or released. Use `index` and `pressed` to identify which button and whether it was pressed or released |
| `handleEncoderPress()` | Gets called when an encoder is pressed or released. Use `index` and `pressed` to identify which encoder and whether is was pressed or released |
| `handleEncoderRotation()` | Gets called when an encoder is rotated. Use `index` and `clockwise` to identify which encoder and in which direction |
| `handleMainGridPress()` | Gets called when a pad in the main grid is pressed or released. Use `x`, `y` and `pressed` to identify which pad and whether it was pressed or released |
| `handleSidebarPress()` | Gets called when a pad in the sidebar is pressed or released. Use `isRightColumn`, `y` and `pressed` to identify which pad and whether it was pressed or released |

### About drawing, rendering and optimization
- I'm using **drawing** when referring to writing to the buffer of the oled/pads, f.e. with `oled.drawLine()` or `mainGrid.setColor()`
- I'm using **rendering** when referring to the contents of those buffers actually being sent to the oled/pads

**Rendering** the oled/pads involves transferring a significant chunk of data and is therefor throttled. By default, both the oled and pads are rendered at 30fps and they are offset from eachother by 15 render-frames

#### There are a few ways to handle drawing:
1. Incidental changes do not need optimization and can go anywhere in your code. For example: changing a pad's color after it was pressed
2. Redrawing the the full oled/grid **every frame** should be handled with a bit more care. `handleRepeatingActions()` will usually get called *a lot* more often than 30fps. Redrawing the full oled/grid every frame would mean you're redrawing many times for each time it is actually rendered, which is a waste of resources. To avoid this, put your drawing code in `drawOled()` or `drawPads()`, as these are called only one time for each time they are rendered
3. The oled and the pads will only **render** if they were drawn to since the last render. So it's best not to redraw or clear them if nothing has changed
4. The pads even go a little further: they are rendered in nine sections of two columns. Each section is only rendered if it was drawn to since the last render. So it's best not to clear the whole grid if you're only updating a small part of it, etc

You'll find an example and some more documentation about optimizing drawing to the oled/grid in [Tech Demo 01](./views/td01_view/)

# Implementing custom UIs and Views
Please refer to the templates and documentation in [uis/_templates/](./uis/_templates/) and [views/_templates](./views/_templates/)
