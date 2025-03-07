#pragma once
#include "utilities/utilities.h"
#include "hardware/pads/main_grid.h"

namespace bareluge {

struct Tetromino {
	// Constant
	static constexpr uint8_t NumVariants = 7;
	static constexpr uint8_t ShapeSize = 5;
	static constexpr uint8_t GridWidth = MainGrid::Height;

	// Properties
	uint8_t variant;                                             // Type
	int8_t x, y;                                                 // Position
	std::array<std::array<uint8_t, ShapeSize>, ShapeSize> shape; // (Rotated) shape
	uint8_t rotation;                                            // Clockwise: 0 .. 3

	void respawn(uint8_t variant, uint8_t& rotationSize, uint8_t& numKickChecks) {
		// Set type, location, rotation
		this->variant = variant;
		x = GridWidth / 2 - (variant == 0 ? 3 : 2);
		y = variant == 0 ? -1 : 0;
		rotation = 0;
		// These variables depend on the spawned variant
		rotationSize = variant == 0 ? 5 : 3;
		numKickChecks = variant == 1 ? 1 : 5;
		// Store shape
		std::copy(std::begin(Tetromino::VariantShape[variant]), std::end(Tetromino::VariantShape[variant]),
		          shape.begin());
	}

	static constexpr RGB VariantColor(uint8_t variant) {
		return RGB::fromHuePastel(variant * (192 / NumVariants));
	}

	// clang-format off
	static constexpr std::array<std::array<std::array<uint8_t, ShapeSize>, ShapeSize>, NumVariants> VariantShape = {{
		// I
		{{
			{{0, 0, 0, 0, 0}},
			{{0, 0, 0, 0, 0}},
			{{0, 1, 1, 1, 1}},
			{{0, 0, 0, 0, 0}},
			{{0, 0, 0, 0, 0}}
		}},

		// O
		{{
			{{0, 1, 1, 0, 0}},
			{{0, 1, 1, 0, 0}},
			{{0, 0, 0, 0, 0}},
			{{0, 0, 0, 0, 0}},
			{{0, 0, 0, 0, 0}}
		}},

		// T
		{{
			{{0, 1, 0, 0, 0}},
			{{1, 1, 1, 0, 0}},
			{{0, 0, 0, 0, 0}},
			{{0, 0, 0, 0, 0}},
			{{0, 0, 0, 0, 0}}
		}},

		// L
		{{
			{{0, 0, 1, 0, 0}},
			{{1, 1, 1, 0, 0}},
			{{0, 0, 0, 0, 0}},
			{{0, 0, 0, 0, 0}},
			{{0, 0, 0, 0, 0}}
		}},

		// J
		{{
			{{1, 0, 0, 0, 0}},
			{{1, 1, 1, 0, 0}},
			{{0, 0, 0, 0, 0}},
			{{0, 0, 0, 0, 0}},
			{{0, 0, 0, 0, 0}}
		}},

		// S
		{{
			{{0, 1, 1, 0, 0}},
			{{1, 1, 0, 0, 0}},
			{{0, 0, 0, 0, 0}},
			{{0, 0, 0, 0, 0}},
			{{0, 0, 0, 0, 0}}
		}},

		// Z
		{{
			{{1, 1, 0, 0, 0}},
			{{0, 1, 1, 0, 0}},
			{{0, 0, 0, 0, 0}},
			{{0, 0, 0, 0, 0}},
			{{0, 0, 0, 0, 0}}
		}}
	}};

	// Kick offset tables:
	// - 3 tables (J L S T Z, I, O)
	// - 4 positions
	// - 5 tries
	// - 2 coordinates
	static constexpr std::array<std::array<std::array<std::array<int8_t, 2>, 5>, 4>, 3> KickOffsetTable = {{
	// J L S T Z
		{{
			// 0
			{{
				{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
			}},
			// R
			{{
				{0, 0}, {1, 0}, {1, -1}, {0, 2}, {1, 2},
			}},
			// 2
			{{
				{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
			}},
			// L
			{{
				{0, 0}, {-1, 0}, {-1, -1}, {0, 2}, {-1, 2},
			}}
		}},
		// I
		{{
			// 0
			{{
				{0, 0}, {-1, 0}, {2, 0}, {-1, 0}, {2, 0},
			}},
			// R
			{{
				{-1, 0}, {0, 0}, {0, 0}, {0, 1}, {0, -2},
			}},
			// 2
			{{
				{-1, 1}, {1, 1}, {-2, 1}, {1, 0}, {-2, 0},
			}},
			// L
			{{
				{0, 1}, {0, 1}, {0, 1}, {0, -1}, {0, 2},
			}}
		}},
		// O
		{{
			// 0
			{{
				{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
			}},
			// R
			{{
				{0, -1}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
			}},
			// 2
			{{
				{-1, -1}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
			}},
			// L
			{{
				{-1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
			}}
		}}
	}};
	// clang-format on
};

} // namespace bareluge
