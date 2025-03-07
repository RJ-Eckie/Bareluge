#pragma once

// Include from community
#include "processing/engines/audio_engine.h"

namespace bareluge {

// Utilities for keeping track of time

namespace TimeKeeper {

// Clock frequency
constexpr inline uint32_t ClockFrequency = 44100;

// Ms to samples multiplier
constexpr inline float M2S = ClockFrequency / 1000.0;

// Samples to ms
constexpr inline uint32_t toMs(uint32_t timeInSamples, bool doExecute = false) {
	return doExecute ? timeInSamples / M2S : timeInSamples;
}

// Ms to samples
constexpr inline uint32_t toSamples(uint32_t timeInMs) {
	return timeInMs * M2S;
}

// System clock
constexpr inline uint32_t time() {
	return AudioEngine::audioSampleTimer;
}

// FPS frame duration
constexpr inline uint32_t frameDuration(uint16_t fps) {
	return ClockFrequency / fps;
}

// Saves system clock in loggedTime variable
constexpr inline void log(uint32_t& loggedTime) {
	loggedTime = time();
}

// Returns how long has passed since loggedTime
constexpr inline uint32_t timeSince(uint32_t referenceTime, bool returnMs = false) {
	return toMs(time() - referenceTime, returnMs);
}

// Returns whether duration has passed since referenceTime (default: since system boot)
constexpr inline bool hasPassed(uint32_t duration, uint32_t referenceTime = 0) {
	return timeSince(referenceTime) >= duration;
}

// Returns true every time duration has passed, logs time to referenceTime if so, does not catch up to slow frames
constexpr inline bool doEvery(uint32_t duration, uint32_t& referenceTime) {
	if (hasPassed(duration, referenceTime)) {
		log(referenceTime);
		return true;
	}
	return false;
}

// Returns true when frame time has passed, logs time to referenceTime if so
// By default tries to catch up on slow frames to reach the exact fps on average
constexpr inline bool doEveryFps(uint32_t fps, uint32_t& referenceTime, bool catchUp = true) {
	uint32_t nextFrameGoalTime = referenceTime + frameDuration(fps);
	// We reached the next frame
	if (time() >= nextFrameGoalTime) {
		// Do we want to catch up and is our overshoot less than a full frame?
		if (catchUp && (timeSince(nextFrameGoalTime) < frameDuration(fps))) {
			// Log the time that this frame was supposed to happen, possibly making the next frame slightly shorter
			referenceTime = nextFrameGoalTime;
		}
		// If our overshoot is larger than a full frame, trying to catch up doesn't make sense and might lead to
		// excessive demands on the cpu. Instead we skip frames and just log the current time. This is also the default
		// behavior when we're not trying to catch up
		else {
			log(referenceTime);
		}
		return true;
	}
	// We didn't reach the next frame
	return false;
}

// Returns how far along we are between [referenceTime] and [referenceTime + timeWindow] (range: 0-1)
constexpr inline double progress(uint32_t timeWindow, uint32_t referenceTime) {
	// Before window
	if (time() <= referenceTime) {
		return 0;
	}
	// After window
	if (time() >= referenceTime + toSamples(timeWindow)) {
		return 1;
	}
	// No window
	if (timeWindow == 0) {
		return time() <= referenceTime ? 0 : 1;
	}
	// If we're still here, progress is safe to calculate
	return timeSince(referenceTime) / static_cast<double>(timeWindow);
}

// Pseudo-random method
inline uint32_t randomTime = 0;
inline uint32_t randomSeed = 0;
constexpr inline uint32_t random(uint32_t range) {
	if (randomTime != time()) {
		randomTime = time();
		randomSeed = time();
	}
	randomSeed ^= (randomSeed << 13);
	randomSeed ^= (randomSeed >> 17);
	randomSeed ^= (randomSeed << 5);
	return randomSeed % range;
}

} // namespace TimeKeeper

// Short alias to easily use time-based utilities
namespace TK = TimeKeeper;

} // namespace bareluge
