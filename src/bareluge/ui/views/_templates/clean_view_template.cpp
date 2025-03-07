#include "clean_view_template.h"

namespace bareluge {

void CleanViewTemplate::init() {
}

void CleanViewTemplate::deinit() {
}

// == View behavior == //

void CleanViewTemplate::handleRepeatingActions() {
}

void CleanViewTemplate::drawOled() {
}

void CleanViewTemplate::drawPads() {
}

void CleanViewTemplate::handleButtonPress(uint8_t index, bool pressed) {
}

void CleanViewTemplate::handleEncoderPress(uint8_t index, bool pressed) {
}

void CleanViewTemplate::handleEncoderRotation(uint8_t index, bool clockwise) {
}

void CleanViewTemplate::handleMainGridPress(uint8_t x, uint8_t y, bool pressed) {
}

void CleanViewTemplate::handleSidebarPress(bool isRightColumn, uint8_t y, bool pressed) {
}

// == Further view implementation below this line == //

} // namespace bareluge
