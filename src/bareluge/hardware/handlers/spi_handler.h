#pragma once
#include "pic_handler.h"
#include "utilities/utilities.h"

namespace bareluge {

// SpiHandler sends data to the oled display and the CV outputs.
//
// This is the loop:
// 1. Add new data to the queue by calling addToQueue()
// 2. Data is saved in the inactive buffer for the chosen destination
//		- If not currently sending data, calls tryForNewTransfer()
// 3. tryForNewTransfer() checks for available data to send
//		- If found, calls initTransfer()
// 4. Active and inactive buffers for chosen destination are swapped
//		- Status is no longer idle, which indicates we are in the sending process
// 5. We request the pic to set the spi pins for the chosen destination and wait for a response
// 6. The response comes from the UI reading out the pic and calling handlePicIndex()
//		- We have to wait a short while after receiving this response
// 7. handleRepeatingActions() checks whether we have waited long enough
//		- If so, calls startTransfer()
// 8. The actual DMA transfer is initiated
// 9. The pic calls interrupt Deluge::isrOledDataTransferComplete() when sending is complete
//		- This calls spiHandler::isrTransferComplete()
// 10. We request the pic to deselect the spi pins for the chosen destination and wait for a response
// 11. The pic response is again received by handlePicIndex(), which calls finishTransfer()
// 12. The next destination is selected for sending, effectively toggling between which destination gets priority
// 13. Status is set back to Idle
// 14. tryForNewTransfer() is called, which either starts a new transfer loop or exits

enum class SpiDestination : uint8_t { Oled, CV };

class SpiHandler {
private:
	enum class SpiHandlerStatus : uint8_t {
		Idle,               // Ready to start new transfer
		WaitForSpiSelect,   // Wait for pic select confirmation
		WaitForSpiSettle,   // Wait for spi pins to settle after select confirmation
		Sending,            // Transfer in progress
		WaitForSpiDeselect, // Wait for pic deselect confirmation
	};

public:
	// Constructor
	SpiHandler(PicHandler& picHandler) : picHandler(picHandler) {}

	// Main method that initiates an spi transfer
	void addToQueue(SpiDestination destination, uint8_t* data);
	// Responds to confirmation from the pic
	void handlePicIndex(uint8_t picIndex);
	// Checks whether spiPinsSettleTime has passed during SpiHandlerStatus::WaitForSpiSettle
	void handleRepeatingActions();
	// Called by Deluge when data transfer is complete
	void isrTransferComplete();

private:
	// Constant
	static constexpr uint16_t OledDataSize = 768; // See Oled::DataSizeBytes

	// RJ: I've had good results with a settle time of 9 samples. Rounding this up to 10 samples (~0.23ms) for safety.
	// The result of this being too long is very obvious: the oled graphics will start offsetting up and to the left,
	// while wrapping around to the right and bottom edges. Resetting the Deluge will reset the display memory
	static constexpr uint16_t SpiPinsSettleTime = 10;

	// Properties
	PicHandler& picHandler;
	SpiHandlerStatus status{SpiHandlerStatus::Idle};
	SpiDestination activeDestination{};

	// Each destination has two (pointers to) data buffers. The active buffer is either currently being sent or has been
	// sent most recently. The inactive buffer receives data from addToQueue()
	std::array<std::array<uint8_t*, 2>, 2> dataBuffer{};
	// Keeps track which buffer is active
	std::array<bool, 2> activeBuffer{};
	// Does the inactive buffer hold new data?
	std::array<bool, 2> inactiveBufferNeedsRendering{};
	// Used to track settle time
	uint32_t spiPinsSelectTime{};

	// Sysex logging / debugging
	uint32_t transferInitTime{};
	uint32_t transferStartTime{};

	// Methods
	void tryForNewTransfer();
	void initTransfer();
	void startTransfer();
	void finishTransfer();
};

} // namespace bareluge
