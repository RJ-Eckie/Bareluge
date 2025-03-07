#pragma once
#include "io/debug/log.h"  // Sysex logging
#include "time_keeper.h" // Timekeeping utilities

namespace bareluge {

// A small collection of useful utilities that are used all over the codebase:
// - Including sysex logging
// - Including timekeeping utilities
// - Quick cast to underlying int value of enums
// - Some wrappers that allow for using std methods with non-identical int types
// - Some handy mapping utilities

template <typename Enum>
constexpr std::underlying_type_t<Enum> to_underlying(Enum e) noexcept {
	return static_cast<std::underlying_type_t<Enum>>(e);
};

template <typename T1, typename T2>
constexpr auto exp_curve(T1 value, T2 max, double power = 2.0) {
	using CommonType = std::common_type_t<std::decay_t<T1>, std::decay_t<T2>>;
	return static_cast<CommonType>(std::pow(static_cast<double>(value) / static_cast<double>(max), power)
	                               * static_cast<double>(max));
}

template <typename T1, typename T2>
constexpr auto log_curve(T1 value, T2 max, double power = 2.0) {
	return exp_curve(value, max, 1.0 / power);
}

template <typename T1, typename T2>
constexpr auto lerp(T1 start, T2 end, double t) {
	using CommonType = std::common_type_t<std::decay_t<T1>, std::decay_t<T2>>;
	return static_cast<CommonType>(std::lerp(static_cast<double>(start), static_cast<double>(end), t));
}

template <typename T1, typename T2>
constexpr auto min(T1 value1, T2 value2) {
	using CommonType = std::common_type_t<std::decay_t<T1>, std::decay_t<T2>>;
	return static_cast<CommonType>(std::min(static_cast<CommonType>(value1), static_cast<CommonType>(value2)));
}

template <typename T1, typename T2>
constexpr auto max(T1 value1, T2 value2) {
	using CommonType = std::common_type_t<std::decay_t<T1>, std::decay_t<T2>>;
	return static_cast<CommonType>(std::max(static_cast<CommonType>(value1), static_cast<CommonType>(value2)));
}

template <typename T1, typename T2, typename T3>
constexpr auto clamp(T1 value1, T2 value2, T3 value3) {
    using CommonType = std::common_type_t<std::decay_t<T1>, std::decay_t<T2>, std::decay_t<T3>>;
    return static_cast<CommonType>(
        std::clamp(static_cast<CommonType>(value1), static_cast<CommonType>(value2), static_cast<CommonType>(value3)));
}

constexpr int32_t lerp(int32_t start, int32_t end, int32_t sourcePosition, uint32_t sourceRange) {
	if (sourceRange == 0) {
		return 0;
	}
	return sourcePosition / static_cast<double>(sourceRange) * (end - start) + start;
}

constexpr float mapSin(float wavePosition, float waveLength, float peakHeight = 1, float valleyHeight = 0,
                       float startAngle = 0) {
	return (std::sin(2 * M_PI * (wavePosition / waveLength) + startAngle) + 1) / 2.0 * (peakHeight - valleyHeight)
	       + valleyHeight;
}

} // namespace bareluge
