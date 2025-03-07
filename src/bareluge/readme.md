# Bareluge firmware for Synthstrom Deluge

A clean and robust framework for building standalone apps on the Deluge hardware

#### Before we begin

1. This firmware is built on top of the Deluge community firmware, which in turn was built on top of the Deluge stock firmware. All props go to those developers and the work they've done
2. For instructions on how to compile custom firmware and install it on your Deluge, please refer to [the community firmware repo](https://github.com/SynthstromAudible/DelugeFirmware)
3. This repo is in early development. While I am trying to keep the repo as consistent as possible, I will take the liberty to structurally reorganise code where I feel it is warranted, at least until we hit v1.0. When I do change things up I will provide instructions on how to update existing apps to work with new firmware versions

   I’m welcoming anyone to play around with this repo but please be aware that during early development **version updates might break your apps**

#### Contents

- [What is Bareluge?](#what-is-bareluge)
- [Getting Started](#getting-started)
- [Core Concepts](#core-concepts)
- [Templates](#templates)
- [References](#references)
- [Examples](#examples)

## What is Bareluge?

Bareluge is designed to be a clean and robust framework for developing standalone apps the Deluge hardware, separate from the existing official and community firmwares. The main priorities when developing this framework are:

- Robust, safe and efficient implementations
- Clear organization of the repo
- Clear documentation of the code

### What is implemented in v0.1?

1. The foundation for the repo and interaction with the Deluge hardware:
     - Basic program structure
     - Newly written handlers for SPI and PIC communication
     - A few reusable utilities / base classes
2. Implementations for the UI elements:
     - Oled display
     - Circular buttons with leds
     - Push encoders
     - Square pads with rgb leds
3. A consistent framwork for developing apps with or without switching views
4. Documentation, templates and examples for all current implementations

### What is not implemented in v0.1?

1. Most notably, IO has not been implemented:
   - Audio in/out
   - Midi in/out
   - CV out
2. Saving to and reading from the SD card has not been implemented
3. The 7-segment display has not been implemented

I/O and SD functionality are planned for v0.2 - no timeframe. I don't have access to a 7-segment display Deluge so it's unlikely that I'll be able to implement it anytime soon

---

## Getting Started
Here's the step-by-step of getting started with developing in Bareluge:
1. Read through the [Core Concepts](#core-concepts). It's not very long and it will make the next steps so much easier if you have a fundamental idea of what is happening
2. Create a new `View` from a [view template](./ui/views/_templates/). The templates include step-by-step instructions on how to create your first `View`. When loading the Bareluge firmware to your Deluge, the View Launcher should now show four colored pads instead of the initial three. Select the fourth pad and press Play to launch it
3. Congrats, You made your first `View`! Use the [reference material](#references) to make it do whatever you want

The included [examples](#examples) range from quite straightforward to pretty complex and should be able to guide you in most of what you're trying to achieve

---

## Core Concepts
There are a few different ways of developing apps in Bareluge and it's important to understand some core concepts to make sure you pick the way that is best for you
### 1. Core Environment
All Bareluge code lives in `src/bareluge/` and is wrapped in a `namespace bareluge`

`bareluge.h` holds a `class Deluge` which only has static members and is the foundational class of everything that happens in Bareluge. After the community firmware runs its boot sequence, a call to `bareluge::Deluge::launch()` makes the code branch off into the Bareluge firmware and stops it from reaching the community firmware main loop

`Deluge` has a property `UI* ui` which is assigned early in `bareluge.cpp`. This assignment defines which UI is loaded and that UI defines all behavior for custom apps
### 2. UI
The `UI` class manages foundational handling of the hardware. It also holds a selection of virtual methods that will be called depending on the device state. To implement behavior, create a class that inherits from `UI`, override those virtual methods and load an instance of your class into `UI* ui` by editing `bareluge.cpp`

A firmware has one type of `UI` during its entire lifetime
### 3. Views
The `View` class implements the same virtual methods as `UI`. To implement behavior, create a class that inherits from `View` and override those virtual methods

Views can be switched at will during runtime. However, only one `View` can be active at any time
### 4. ViewUI
`ViewUI` inherits from `UI` and can therefor be loaded into `UI* ui` in `Deluge`. `ViewUI` has the necessary implementations to make use of views. If you want to use views, create a class that inherits from `ViewUI`, load an instance of that class into `UI* ui` and use the `ViewUI`'s `launchView()` method to launch a `View` of your choice
### 5. ViewLauncherUI
The `ViewLauncherUI` is a `ViewUI` and is the default `UI` of Bareluge. It maps out all implemented views on the pads of the main grid, which functions as a launchpad. Press one of the lit pads to select a view and press Play to launch it.

`ViewLauncherUI` is especially useful for testing prototypes as it allows you to run views without having to create your own custom `ViewUI`. The drawback is that you lose the flexibility of using different views in your app as the `ViewLauncherUI` only launches singular, unconnected views
### 6. Choosing your method
There are ultimately three ways of running code in Bareluge:
1. Create a class that inherits from `UI` and implement behavior in it. Good for simple apps with limited functionality
2. Create a class that inherits from `ViewUI` and create one or more classes that inherit from `View`. Implement behavior into each of your `View` classes and use your `ViewUI` class to switch between your views. Good for more complex apps, allows separation of different functionalities or modes
3. Create a class that inherits from `View` and run it from the `ViewLauncherUI`. Good for getting familiar with Bareluge and testing view implementations

---

## Templates
| Folder | Description |
| - | - |
| [ui/uis/_templates](./ui/uis/_templates/) | Templates for creating new `UI`s and `ViewUI`s. Includes step-by-step instructions in the readme |
| [ui/views/_templates](./ui/views/_templates/) | Templates for creating new `View`s. Include step-by-step instructions in the readme |
## References
| Folder | Description |
| - | - |
| [hardware/](./hardware/) | Code reference for working with hardware |
| [ui/](./ui/) | Code reference for working with uis and views |
| [utilities/](./utilities) | List of utilities |
## Examples
The examples all have their own documentation readme files
| Folder | Description |
| - | - |
| [ui/uis/hardware_test_ui/](./ui/uis/hardware_test_ui/) | `HardwareTestUI` is a fairly simple implementation of a `UI`. It tests all currently implemented UI elements |
| [ui/views/hardware_test_view/](./ui/views/hardware_test_view/) | `HardwareTestView` is identical to `HardwareTestUI` in functionality, but implemented as a `View` instead of a `UI`. You can compare the two to see the (few) differences between a `View` and a `UI` implementation |
| [ui/views/td01_view](./ui/views/td01_view/) | `TD01_View` (Tech Demo 01 View) is a bitmap editor for a 63x20 pixel canvas. It was written to provide an example of a more complex app and demonstrates some more advanced implementations |
| [ui/views/tetris_view](./ui/views/tetris_view/) | `TetrisView` is probably exactly what you expect it to be |
| [ui/uis/view_launcher_ui/](./ui/uis/view_launcher_ui/) | `ViewLauncherUI` is the default UI for Bareluge. It wasn't specifically written as an example, but its code can be used as a reference for a simple implementation of a `ViewUI`, including examples of how to separate global behavior from view-dependent behavior |
| [ui/views/view_selector_view](./ui/views/view_selector_view/) | `ViewSelectorView` is the default view of `ViewLauncherUI`, the two of them together make up the full view launching system |
---
[back to top](#contents)
