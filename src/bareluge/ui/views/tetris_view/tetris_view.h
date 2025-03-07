#pragma once
#include "tetromino.h"
#include "ui/views/view.h"

namespace bareluge {

// Tetris tech demo
//
// Turn the Deluge 90 degrees so the sidebar is closest to you
// - The three lit buttons in the sidebar, left to right, are:
// 		- Move left
//		- Drop
//		- Move right
// Rotate your tetromino by rotating the Tempo encoder
// The oled display shows the upcoming tetromino
//
// While all basic tetris mechanics (including wall-kicks) are implemented, this is still a tech demo. There is only one
// speed and there are no scores

// RJ: This is one of the first things I tried to write for Bareluge purely from an "I wonder if I could?" perspective.
// I thought it would be pretty cool and pretty silly if you'd be able to play games on the Deluge, and what better to
// start with than one of the OGs of all games, Tetris?

class TetrisView : public View {
public:
	// Constructor
	TetrisView(UiElements& uiElements) : View(ViewType::Base, uiElements) {}

	// Called when view is created
	void init() override;
	// Called every frame
	void handleRepeatingActions() override;

	// Called right before pads are rendered
	void drawPads() override;

	// Called on encoder rotation
	void handleEncoderRotation(uint8_t index, bool clockwise) override;
	// Called on pad press or release in sidebar
	void handleSidebarPress(bool isRightColumn, uint8_t y, bool pressed) override;

private:
	// Game states
	enum class GameState : uint8_t { InitScreen, InGame, ClearLinesAnimation, GameOverAnimation };

	// View constants
	static constexpr uint8_t GridWidth = MainGrid::Height;
	static constexpr uint8_t GridHeight = MainGrid::Width;

	// Color constants
	static constexpr RGB BackgroundColor = RGB{};
	static constexpr RGB MoveButtonColor = RGB(81, 103, 220);
	static constexpr RGB DropButtonColor = RGB(54, 212, 96);
	static constexpr RGB ClearLineBlinkColor = RGB::monochrome(192);

	// Game speed constants
	static constexpr uint32_t DefaultFrameDelay = TK::toSamples(500); // Default frame length
	static constexpr uint32_t DropFrameDelay = TK::toSamples(15);     // Frame length while dropping

	// Controls
	static constexpr uint8_t LeftButtonY = 0;
	static constexpr uint8_t DropButtonY = 1;
	static constexpr uint8_t RightButtonY = 2;
	static constexpr uint32_t HoldDelay = TK::toSamples(200);  // A press is a hold after this time
	static constexpr uint32_t RepeatDelay = TK::toSamples(75); // A hold repeats with this speed

	// Tetris properties
	GameState gameState{};
	Tetromino tet{};                                                 // Moving tetromino
	std::array<std::array<RGB, GridHeight>, GridWidth> fieldColor{}; // Fallen tetrominos
	uint8_t nextTetVariant{};                                        // Show this in oled
	uint8_t rotationSize{};   // Either 5 or 3, depends on the size of the tetromino
	uint8_t numKickChecks{};  // Depends on tetromino variant
	uint32_t frameDelay{};    // Length of frame
	uint32_t lastFrameTime{}; // Last time the tetromino moved through gravity
	uint32_t moveLeftLastTriggered{};
	uint32_t moveRightLastTriggered{};

	// Clearing lines
	std::vector<uint8_t> clFullLineY{};
	uint32_t clAnimStart{};
	uint32_t clAnimLastBlink{};

	// Game over
	std::vector<std::pair<uint8_t, uint8_t>> goPadsToFill{};
	uint32_t goStartTime{};
	uint8_t goTotalPadsToFill{};

	// Game states
	bool fieldFilled(int8_t x, int8_t y);
	bool lineFilled(uint8_t y);
	bool isColliding(Tetromino testTet);

	// Tetris drawing
	void drawControls();
	void drawTet(bool doErase = false);
	void drawField();
	void clearLinesAnimation();
	void drawTetrominoSquare(uint8_t x, uint8_t y, uint8_t tetLength = 3, uint8_t tetWidth = 2);
	void drawUpcomingTetromino();
	void gameOver();
	void gameOverAnimation();

	// Tetris actions
	void initGame();
	void spawnTetromino(bool firstSpawn = false);
	void handleHolds();
	void handleGameFrame();
	void moveTetromino(bool toTheLeftToTheLeft);
	void rotateTetromino(bool toTheLeftToTheLeft);
	void placeTetromino();
	void checkForFullLines();

	// Helper: draw in portrait orientation
	void drawColor(uint8_t x, uint8_t y, RGB color) {
		if (x < GridWidth && y < GridHeight) {
			mainGrid.setColor(y, x, color);
		}
	}
};

} // namespace bareluge
