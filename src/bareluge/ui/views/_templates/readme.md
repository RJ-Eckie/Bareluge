# View Templates
There are two versions here:
| Class | Description |
| - | - |
| `ViewTemplate` | Provides implementations for all `View` methods. All non-repeating methods produce a sysex-logging message when triggered. The press/rotation methods provide a bit of example code to demonstrate how you could go about implementing behavior. Good for getting started |
| `CleanViewTemplate` | Provides empty implementations for all `View` methods. More efficient when you no longer need the guidance |

## Creating a new `View` from a view template

This guide uses `ViewTemplate`, but using `CleanViewTemplate` works identically

### 1. Copying files
Let's say we're implementing `ExampleView`. Copy `template_view.h` and `template_view.cpp` to a new folder named `ui/views/example_view/`

### 2. Renaming
Everything "ViewTemplate" needs to be renamed to "ExampleView":
- Rename the copied files to `example_view.h` and `example_view.cpp`
- Change the `#include` in `example_view.cpp` to `#include "example_view.h"`
- Replace all occurences of `ViewTemplate` in the two files with `ExampleView`

These steps are enough to have created a new valid view. For `ViewUI` and `ViewLauncherUI` to recognise it, one more step needs to happen:

### 3. Adding the `View` to Bareluge
- Add `ExampleView` to `enum class ViewType{}` in [`ui/views/view.h`](../view.h)
- Add `#include "example_view/example_view.h"` to [`ui/views/view_list.h`](../view_list.h)
- Add `case ViewType::ExampleView: return new ExampleView(uiElements);` to the switch statement in [`ui/views/view_list.h`](../view_list.h)

A `ViewType::ExampleView` can now be launched by `ViewUI::LaunchView()` and `ExampleView` will automatically be included in the `ViewSelectorView`
