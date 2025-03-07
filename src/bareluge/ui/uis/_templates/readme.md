# UI Templates
There are four versions here:
| Class | Description |
| - | - |
| `UITemplate` | Provides implementations for all `UI` methods. All non-repeating methods produce a sysex-logging message when triggered. The press/rotation methods provide a bit of example code to demonstrate how you could go about implementing behavior. Good for getting started |
| `CleanUITemplate` | Provides empty implementations for all `UI` methods. More efficient when you no longer need the guidance |
| `ViewUITemplate` | Very similar to `UITemplate`, but inherits from `ViewUI` instead. The default behavior of a `ViewUI` is to forward all `UI` calls to the active `View`. When any of the `ViewUI` methods is overridden, the override replaces that default behavior. For that reason most of the `ViewUI` methods are commented out by default. `handleButtonPress()` demonstrates an example of handling one button on a `UI` level, while all other button interactions are forwarded to the active view. Good for getting started |
| `CleanViewUITemplate` | Provides commented out, empty implementations for all `ViewUI` methods. More efficient when you no longer need the guidance |

## Creating a new `UI` from a UI template

This guide uses `UITemplate`, but using any of the other templates works identically

### 1. Copying files
Let's say we're implementing `ExampleUI`. Copy `ui_template.h` and `ui_template.cpp` to a new folder named `ui/uis/example_ui/`

### 2. Renaming
Everything "UITemplate" needs to be renamed to "ExampleUI":
- Rename the copied files to `example_ui.h` and `example_ui.cpp`
- Change the `#include` in `example_ui.cpp` to `#include "example_ui.h"`
- Replace all occurences of `UITemplate` in the two files with `ExampleUI`

### 3. Adding your UI to Bareluge
Add `#include "example_ui/example_ui.h"` to [ui/uis/ui_list.h](../ui_list.h)

### 4. Making Deluge run your UI
Early in [`bareluge.cpp`](../../../bareluge.cpp) find the line where `UI* Deluge::ui` is assigned and change that line to:
- `UI* Deluge::ui new ExampleUI(Deluge::picHandler, Deluge::spiHandler);`

That's it! You're now running your own custom `UI`

### 5. Extra step for `ViewUI`s
In most cases you'll want to load a custom `View` as soon as your custom `ViewUI` gets loaded. To make that happen, call `launchView(ViewType::ExampleView)` in the `init()` method of your `ViewUI`
