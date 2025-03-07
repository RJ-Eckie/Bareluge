# Working with Hardware
- [Hardware Implementations](#hardware-implementations)
- [Interacting with Hardware](#interacting-with-hardware)
- [Low Level Hardware Handlers](#low-level-hardware-handlers)

## Hardware Implementations

| Folder | Class | Description |
|----|----|----|
|  `buttons/` | `Button` | Represents a circular button with binary led. Is a `Pressable`, identified by a `ButtonName` |
| `display/7seg/` | - | The 7 segment display is currently not implemented |
| `display/oled/` | `Oled` | Manages the oled display |
| `encoders/` | `Encoder` | Encoder base class. Is a `Pressable`, identified by an `EncoderName` |
| `encoders/` | `DetentedEncoder` | Represents a black, non-detented encoder. Is an `Encoder`  |
| `encoders/` | `NonDetentedEncoder` | Base class of gold encoders. Is an `Encoder`  |
| `encoders/` | `GoldEncoder` | Represents a gold, non-detented encoder with a led bar. Is a `NonDetentedEncoder` and holds a `LedBar`  |
| `encoders/` | `LedBar` | Represents a vertical bar of four dimmable leds to the left of a gold encoder. A value can be displayed on them by calling `goldEncoder.setLedBarValue()`. Retrieve a reference to the `LedBar` object itself to gain more granular control over each of the four leds |
| `pads/` | `Grid` | Base class for a matrix of rgb pads. The name "Grid" is inherited from the community firmware |
| `pads/` | `Pads` | The `UI` uses this to handle reading from and writing to all pressable rgb pads. Is a `Grid`.  Use `MainGrid` and `Sidebar` instead of this, they will in turn call `Pads` |
| `pads/` | `MainGrid` | Represents the main, 16 x 8 pad grid of pressable rgb pads. Is a `Grid` |
| `pads/` | `Sidebar` | Represents the 2 x 8 pad grid of pressable rgb pads to the right of the main grid. Is a `Grid` |


## Interacting with Hardware

The following properties can be used to interact with the Deluge hardware in uis and views. Some of these may be either objects or references to objects, depending on whether you are implementing a `UI` or a `View`. (Their usage is identical in either situation)

| Reference | Description |
|----|----|
| `Oled oled` | The oled display |
| `MainGrid mainGrid` | The main grid |
| `SideBar sidebar` | The sidebar |
| `Button& getButton()` | Returns a reference to a `Button`, pass either an index or a `ButtonName` to select which one |
| `Encoder& getEncoder()` | Returns a reference to an `Encoder`, pass either an index or an `EncoderName` to select which one |
| `GoldEncoder& getGoldEncoder()` | Returns a reference to a `GoldEncoder`, pass a bool to select which one |

## Low Level Hardware Handlers
| Folder | Class | Description |
|----|----|----|
| `handlers/` | `PicHandler` | Communicates with the `PIC24FJ256`, which handles reading out presses, setting led values and configuring SPI pins |
| `handlers/` | `SpiHandler` | Sends data over SPI. This is used to write data to the oled display. In the future, this will also be used for CV output |
