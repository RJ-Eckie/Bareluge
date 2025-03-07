#pragma once
#include "led_bar.h"
#include "non_detented_encoder.h"

namespace bareluge {

// Gold encoder (non-detented) with its led bar

class GoldEncoder : public NonDetentedEncoder {
public:
	// Constructor
	GoldEncoder(PicHandler& picHandler, bool isTopGoldEncoder)
	    : NonDetentedEncoder(isTopGoldEncoder ? to_underlying(EncoderName::GoldTop)
	                                          : to_underlying(EncoderName::GoldBottom)),
	      ledBar(picHandler, isTopGoldEncoder) {}

	// Led bar

	LedBar& getLedBar() { return ledBar; }

	void clearLedBar() { ledBar.clear(); }

	void setLedBarValue(uint8_t value, bool isUnipolar = true) { ledBar.setValue(value, isUnipolar); }

private:
	// Properties
	LedBar ledBar;
};

} // namespace bareluge
