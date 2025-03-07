## Example `View` - `HardwareTestView`

### Intentions

- A simple `View` implementation that tests all hardware controls
- Demonstrating the differences between a UI implementation and a view implementation

### What it does

- Every input-control outputs a message over sysex logging when pressed/rotated
- Pressing any pad or button toggles its led
- The oled display shows a crosshair with a box at its center
  - Turning the Vertical/Horizontal encoders moves the crosshair
  - Turning the Tempo encoder changes the size of the box
  - Pressing any of these encoders resets its parameter to a default value
- The gold encoders adjust the value displayed on their led bars
  - Pressing a gold encoder switches between showing the value as a unipolar and as a bipolar value
- The mod-button leds (Level/Pan through Custom2/Custom3) represent a binary value, which can be adjusted by rotating the Select encoder
  - Pressing the Select encoder resets the value to zero
  - The buttons can be pressed to toggle bits in the value

### Notes

`HardwareTestView` also exists as a `UI` implementation in [ui/uis/hardware_test_ui/](../../uis/hardware_test_ui/). You can compare these to see the (few) differences between a UI implementation and a view implementation
