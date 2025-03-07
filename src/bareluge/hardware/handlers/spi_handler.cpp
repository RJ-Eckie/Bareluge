#include "spi_handler.h"

// Low-level includes
extern "C" {
#include "RZA1/compiler/asm/inc/asm.h" // Used for DMA transfer
}
#include "RZA1/system/iobitmasks/dmac_iobitmask.h" // Used for DMA transfer

namespace bareluge {

// The entire spi transfer loop is laid out below in chronological order

void SpiHandler::addToQueue(SpiDestination destination, uint8_t* data) {
	// If we already have data saved to be sent
	if (inactiveBufferNeedsRendering[to_underlying(destination)]) {
		D_PRINT("== SPI Handler Overload ==");
		// This is not technically a problem. The saved data will be overwritten, which is probably the intended
		// behavior in this situation
	}
	// Save the data to the inactive buffer of the current destination
	dataBuffer[to_underlying(destination)][!activeBuffer[to_underlying(destination)]] = data;
	// Mark as needing to be rendered
	inactiveBufferNeedsRendering[to_underlying(destination)] = true;
	// Activate transfer loop
	if (status == SpiHandlerStatus::Idle) {
		tryForNewTransfer();
	}
}

void SpiHandler::tryForNewTransfer() {
	// Active destination needs rendering
	if (inactiveBufferNeedsRendering[to_underlying(activeDestination)]) {
		// Init transfer
		initTransfer();
		return;
	}
	// Try other destination
	activeDestination = static_cast<SpiDestination>(!to_underlying(activeDestination));
	// Other destination needs rendering
	if (inactiveBufferNeedsRendering[to_underlying(activeDestination)]) {
		// Init transfer
		initTransfer();
		return;
	}
	// Switch back to original active destination
	activeDestination = static_cast<SpiDestination>(!to_underlying(activeDestination));
}

void SpiHandler::initTransfer() {
	// Flip the inactive page to active
	activeBuffer[to_underlying(activeDestination)] = !activeBuffer[to_underlying(activeDestination)];
	// Mark the now inactive page as not needing to be rendered
	inactiveBufferNeedsRendering[to_underlying(activeDestination)] = false;
	// Depending on the destination
	switch (activeDestination) {
	case SpiDestination::Oled:
		// Tell pic to open oled spi
		picHandler.send(PicMessage::OledSpiPinsSelect);
		break;
	case SpiDestination::CV:
		// Not implemented yet
		break;
	}
	// Wait for pic to confirm
	status = SpiHandlerStatus::WaitForSpiSelect;
}

void SpiHandler::handlePicIndex(uint8_t picIndex) {
	switch (status) {
	case SpiHandlerStatus::WaitForSpiSelect:
		switch (static_cast<SpiDestination>(activeDestination)) {
		case SpiDestination::Oled:
			// Pic confirmed our request
			if (picIndex == to_underlying(PicMessage::OledSpiPinsSelect)) {
				// A short delay is needed here to make sure the spi pins are successfully set
				status = SpiHandlerStatus::WaitForSpiSettle;
				TK::log(spiPinsSelectTime);
			}
			else {
				D_PRINT("Warning: Unexpected pic message in spi handler");
			}
			break;
		case SpiDestination::CV:
			// Not implemented yet
			break;
		}
		break;
	case SpiHandlerStatus::WaitForSpiDeselect:
		switch (static_cast<SpiDestination>(activeDestination)) {
		case SpiDestination::Oled:
			// Pic confirmed our request
			if (picIndex == to_underlying(PicMessage::OledSpiPinsDeselect)) {
				// Finish up
				finishTransfer();
			}
			else {
				D_PRINT("Warning: Unexpected pic message in spi handler");
			}
			break;
		case SpiDestination::CV:
			// Not implemented yet
			break;
		}
		break;
	}
}

void SpiHandler::handleRepeatingActions() {
	// Are we waiting for the spi pins to settle?
	if (status == SpiHandlerStatus::WaitForSpiSettle
	    // And the settle time has passed?
	    && TK::hasPassed(SpiPinsSettleTime, spiPinsSelectTime)) {
		// Start data transfer
		startTransfer();
	}
}

void SpiHandler::startTransfer() {
	uint16_t transferDataSize;
	uint32_t transferDataAddress;
	TK::log(transferStartTime);
	// Start the dma transfer (code adapted from oled_low_level.c)
	switch (static_cast<SpiDestination>(activeDestination)) {
	case SpiDestination::Oled:
		transferDataSize = OledDataSize;
		transferDataAddress = (uint32_t)
		    dataBuffer[to_underlying(SpiDestination::Oled)][activeBuffer[to_underlying(SpiDestination::Oled)]];
		RSPI(SPI_CHANNEL_OLED_MAIN).SPDCR = 0x20u;               // 8-bit
		RSPI(SPI_CHANNEL_OLED_MAIN).SPCMD0 = 0b0000011100000010; // 8-bit
		RSPI(SPI_CHANNEL_OLED_MAIN).SPBFCR.BYTE = 0b01100000;    // 0b00100000;
		DMACn(OLED_SPI_DMA_CHANNEL).N0TB_n = transferDataSize;
		DMACn(OLED_SPI_DMA_CHANNEL).N0SA_n = transferDataAddress;
		v7_dma_flush_range(transferDataAddress, transferDataAddress + transferDataSize);
		DMACn(OLED_SPI_DMA_CHANNEL).CHCTRL_n |=
		    DMAC_CHCTRL_0S_CLRTC | DMAC_CHCTRL_0S_SETEN; // ---- Enable DMA Transfer and clear TC bit ----
		break;
	case SpiDestination::CV:
		// Not implemented yet
		break;
	}
	status = SpiHandlerStatus::Sending;
}

void SpiHandler::isrTransferComplete() {
	if (status == SpiHandlerStatus::Sending) {
		switch (static_cast<SpiDestination>(activeDestination)) {
		case SpiDestination::Oled:
			// Tell pic to close oled spi
			picHandler.send(PicMessage::OledSpiPinsDeselect);
			break;
		case SpiDestination::CV:
			// Not implemented yet
			break;
		}
		// Wait for pic to confirm
		status = SpiHandlerStatus::WaitForSpiDeselect;
	}
	else {
		D_PRINT("Invalid spi handler isr trigger");
	}
}

void SpiHandler::finishTransfer() {
	// Try the other destination first next time
	activeDestination = static_cast<SpiDestination>(!to_underlying(activeDestination));
	// Back to default status
	status = SpiHandlerStatus::Idle;
	// Check whether we need to start a new spi transfer
	tryForNewTransfer();
}

} // namespace bareluge
