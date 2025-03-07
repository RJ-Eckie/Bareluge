#include "tetris_view.h"

namespace bareluge {

// Called when view is created
void TetrisView::init() {
	initGame();
}

// Called every frame
void TetrisView::handleRepeatingActions() {
	switch (gameState) {
	case GameState::InGame:
		handleHolds();
		handleGameFrame();
		break;
	case GameState::ClearLinesAnimation:
		clearLinesAnimation();
		break;
	case GameState::GameOverAnimation:
		gameOverAnimation();
		break;
	}
}

// Called right before pads are rendered
void TetrisView::drawPads() {
	if (gameState == GameState::InGame) {
		mainGrid.clear();
		drawControls();
		drawField();
		drawTet();
	}
}

// Called on encoder rotation
void TetrisView::handleEncoderRotation(uint8_t index, bool clockwise) {
	if (index == to_underlying(EncoderName::Tempo)) {
		rotateTetromino(!clockwise);
	}
}

// Called on pad press or release in sidebar
void TetrisView::handleSidebarPress(bool isRightColumn, uint8_t y, bool pressed) {
	if (isRightColumn) {
		switch (y) {
		case LeftButtonY:
			if (pressed) {
				moveTetromino(true);
			}
			break;
		case RightButtonY:
			if (pressed) {
				moveTetromino(false);
			}
			break;
		case DropButtonY:
			// Switches between default speed and drop speed
			frameDelay = pressed ? DropFrameDelay : DefaultFrameDelay;
			break;
		}
	}
}

// == Tetris Methods == //

bool TetrisView::fieldFilled(int8_t x, int8_t y) {
	// Out of bounds
	if (x < 0 || x >= GridWidth || y < 0 || y >= GridHeight) {
		// This is "solid" and counts as filled
		return true;
	}
	// In bounds, if any non-zero color is held, this is filled
	return (fieldColor[x][y].r + fieldColor[x][y].g + fieldColor[x][y].b);
}

bool TetrisView::lineFilled(uint8_t y) {
	for (uint8_t x = 0; x < GridWidth; x++) {
		if (!fieldFilled(x, y)) {
			return false;
		}
	}
	return true;
}

bool TetrisView::isColliding(Tetromino testTet) {
	for (uint8_t x = 0; x < Tetromino::ShapeSize; x++) {
		for (uint8_t y = 0; y < Tetromino::ShapeSize; y++) {
			if (testTet.shape[y][x] && fieldFilled(testTet.x + x, testTet.y + y)) {
				// An existing block in the testTet shape overlaps with a solid part of the playing field. This is a
				// collision
				return true;
			}
		}
	}
	// No collisions found
	return false;
}

void TetrisView::initGame() {
	// Clear the view
	clearView();
	// Clear playing field
	fieldColor = {};
	// Spawn first tetromino
	spawnTetromino(true);
	// Start game
	gameState = GameState::InGame;
}

void TetrisView::spawnTetromino(bool firstSpawn) {
	if (firstSpawn) {
		// On the first spawn we need to generate a new nextTetVariant as well
		nextTetVariant = TK::random(Tetromino::NumVariants);
	}
	// Spawn tetromino of type nextTetVariant
	tet.respawn(nextTetVariant, rotationSize, numKickChecks);
	// A collision on spawn ends the game
	if (isColliding(tet)) {
		gameOver();
		return;
	}
	// Define upcoming tetromino type
	nextTetVariant = TK::random(Tetromino::NumVariants);
	// Draw it on the oled display
	drawUpcomingTetromino();
	// Reset frame speed in case we ended on a drop
	frameDelay = DefaultFrameDelay;
	// Simulates left/right being pressed newly on new tetromino
	sidebar.resetHold(1, LeftButtonY);
	sidebar.resetHold(1, RightButtonY);
}

void TetrisView::handleHolds() {
	// Is left being held?
	if (sidebar.getHoldTime(1, LeftButtonY) >= HoldDelay) {
		// Move left repeatedly
		if (TK::doEvery(RepeatDelay, moveLeftLastTriggered)) {
			moveTetromino(true);
		}
	}
	// Is right being held?
	if (sidebar.getHoldTime(1, RightButtonY) >= HoldDelay) {
		// Move right repeatedly
		if (TK::doEvery(RepeatDelay, moveRightLastTriggered)) {
			moveTetromino(false);
		}
	}
}

void TetrisView::handleGameFrame() {
	if (TK::doEvery(frameDelay, lastFrameTime)) {
		// Move tetromino down
		tet.y++;
		// Check for collisions
		if (isColliding(tet)) {
			// Undo drop
			tet.y--;
			// Solidify tetromino in playing field
			placeTetromino();
			// Check for full lines
			checkForFullLines();
		}
	}
}

void TetrisView::drawControls() {
	sidebar.setColor(1, LeftButtonY, MoveButtonColor);
	sidebar.setColor(1, RightButtonY, MoveButtonColor);
	sidebar.setColor(1, DropButtonY, DropButtonColor);
}

void TetrisView::drawTet(bool doErase) {
	// Loop through tetromino cells
	for (uint8_t x = 0; x < Tetromino::ShapeSize; x++) {
		for (uint8_t y = 0; y < Tetromino::ShapeSize; y++) {
			// Check if the cell is active
			if (tet.shape[y][x]) {
				// Negative tet values wrap around here, which is an implicit minimum value check
				uint8_t padX = tet.x + x;
				uint8_t padY = tet.y + y;
				// Maximum value check
				if (padX < GridWidth && padY < GridHeight) {
					drawColor(padX, padY, doErase ? BackgroundColor : Tetromino::VariantColor(tet.variant));
				}
			}
		}
	}
}

void TetrisView::drawField() {
	// Render the contents of fieldColor
	for (uint8_t x = 0; x < GridWidth; ++x) {
		for (uint8_t y = 0; y < GridHeight; ++y) {
			if (fieldFilled(x, y)) {
				drawColor(x, y, fieldColor[x][y]);
			}
		}
	}
}

void TetrisView::moveTetromino(bool toTheLeftToTheLeft) {
	// Log time of move
	TK::log(toTheLeftToTheLeft ? moveLeftLastTriggered : moveRightLastTriggered);
	// Move block
	tet.x += toTheLeftToTheLeft ? -1 : 1;
	// Unless there's a collision there
	if (isColliding(tet)) {
		// In wich case we move right back to where we started
		tet.x += toTheLeftToTheLeft ? 1 : -1;
	}
}

void TetrisView::rotateTetromino(bool toTheLeftToTheLeft) {
	// Create test tetromino
	Tetromino testTet = tet;
	// Rotate it
	testTet.rotation = (testTet.rotation + 4 + (toTheLeftToTheLeft ? -1 : 1)) % 4;
	for (uint8_t bx = 0; bx < rotationSize; bx++) {
		for (uint8_t by = 0; by < rotationSize; by++) {
			testTet.shape[by][bx] =
			    toTheLeftToTheLeft ? tet.shape[bx][rotationSize - 1 - by] : tet.shape[rotationSize - 1 - bx][by];
		}
	}
	// Setting up the validation process
	bool foundValidRotation = false;
	uint8_t numKickCheck = 0;
	int8_t xOffset = 0;
	int8_t yOffset = 0;
	uint8_t tableID;

	// Defining the righ kick offset table
	switch (testTet.variant) {
	case 0:
		tableID = 1;
		break;
	case 1:
		tableID = 2;
		break;
	default:
		tableID = 0;
		break;
	}

	// Loop through the kick checks
	while ((numKickCheck < numKickChecks) && !foundValidRotation) {
		// Reset position, subtract offset from previous check
		testTet.x -= xOffset;
		testTet.y -= yOffset;
		// Take offsets from table
		xOffset = Tetromino::KickOffsetTable[tableID][tet.rotation][numKickCheck][0]
		          - Tetromino::KickOffsetTable[tableID][testTet.rotation][numKickCheck][0];
		yOffset = -(Tetromino::KickOffsetTable[tableID][tet.rotation][numKickCheck][1]
		            - Tetromino::KickOffsetTable[tableID][testTet.rotation][numKickCheck][1]);
		// Add offsets
		testTet.x += xOffset;
		testTet.y += yOffset;
		// Check for collisions
		foundValidRotation = !isColliding(testTet);
		// Next check please!
		numKickCheck++;
	}

	// If we found a valid solution
	if (foundValidRotation) {
		// we apply it to our tetromino
		tet = testTet;
	}
	// If we do not find a valid solution, we don't touch our tetromino
}

void TetrisView::placeTetromino() {
	for (uint8_t x = 0; x < Tetromino::ShapeSize; x++) {
		for (uint8_t y = 0; y < Tetromino::ShapeSize; y++) {
			if (tet.shape[y][x]) {
				fieldColor[tet.x + x][tet.y + y] = Tetromino::VariantColor(tet.variant);
			}
		}
	}
}

void TetrisView::checkForFullLines() {
	// Clear previous results
	clFullLineY.clear();
	// Start checking lines from the bottom
	int8_t y = GridHeight - 1;
	// To the top, or max 3 lines above the first found full line
	while ((y >= 0) && (clFullLineY.size() == 0 || y >= clFullLineY[0] - 3)) {
		if (lineFilled(y)) {
			// Save y position of full line
			clFullLineY.insert(clFullLineY.begin(), y);
		}
		y--;
	}
	// If we have any lines to clear
	if (clFullLineY.size() > 0) {
		// Set up clear lines animation
		mainGrid.clear();
		drawField();
		TK::log(clAnimStart);
		clAnimLastBlink = 0;
		gameState = GameState::ClearLinesAnimation;
	}
	else {
		// Otherwise, spawn new tetromino
		spawnTetromino();
	}
}

void TetrisView::clearLinesAnimation() {
	static constexpr uint32_t BlinkDelay = TK::toSamples(75);
	static constexpr uint32_t BlinkRepeats = 3;

	// End of animation
	if (TK::hasPassed(2 * BlinkRepeats * BlinkDelay, clAnimStart)) {
		// Actually clear the lines:
		for (uint8_t fLY : clFullLineY) {
			// Loop through all lines above full line
			for (int8_t y = fLY - 1; y >= 0; y--) {
				// Move entire line down one pad
				for (uint8_t x = 0; x < GridWidth; x++) {
					fieldColor[x][y + 1] = fieldColor[x][y];
				}
			}
			// Clear the top row
			for (uint8_t x = 0; x < GridWidth; x++) {
				fieldColor[x][0] = BackgroundColor;
			}
		}
		// Respawn tetromino
		spawnTetromino();
		// Return to game
		gameState = GameState::InGame;
		return;
	}

	// Blink lines
	if (TK::doEvery(BlinkDelay, clAnimLastBlink)) {
		// First part of the blink
		if (TK::timeSince(clAnimStart) / BlinkDelay % 2) {
			// Light up full lines
			for (uint8_t y : clFullLineY) {
				for (uint8_t x = 0; x < GridWidth; x++) {
					drawColor(x, y, ClearLineBlinkColor);
				}
			}
		}
		// Second part of the blink
		else {
			// Draw full lines in dimmed original color
			for (uint8_t y : clFullLineY) {
				for (uint8_t x = 0; x < GridWidth; x++) {
					drawColor(x, y, fieldColor[x][y].dim(6));
				}
			}
		}
	}
}

// Draw one square of a tetromino on the oled display
void TetrisView::drawTetrominoSquare(uint8_t x, uint8_t y, uint8_t tetLength, uint8_t tetWidth) {
	static constexpr uint8_t squareSize = 21;
	uint8_t startX = Oled::Width / 2 - squareSize * tetLength / 2;
	uint8_t startY = Oled::Height / 2 - squareSize * tetWidth / 2;
	oled.drawRect(startX + x * squareSize + 1, startY + y * squareSize + 1, squareSize - 2, squareSize - 2);
	oled.fillRect(startX + x * squareSize + 4, startY + y * squareSize + 4, squareSize - 8, squareSize - 8);
}

// Draw the upcoming tetromino to the oled display
void TetrisView::drawUpcomingTetromino() {
	oled.clear();
	switch (nextTetVariant) {
	case 0:
		// I
		for (uint8_t x = 0; x < 4; x++) {
			drawTetrominoSquare(x, 0, 4, 1);
		}
		break;
	case 1:
		// O
		drawTetrominoSquare(0, 0, 2, 2);
		drawTetrominoSquare(0, 1, 2, 2);
		drawTetrominoSquare(1, 0, 2, 2);
		drawTetrominoSquare(1, 1, 2, 2);
		break;
	default:
		// J L S T Z
		for (uint8_t x = 0; x < 3; x++) {
			for (uint8_t y = 0; y < 2; y++) {
				if (Tetromino::VariantShape[nextTetVariant][y][x]) {
					drawTetrominoSquare(x, y);
				}
			}
		}
		break;
	}
}

void TetrisView::gameOver() {
	// Save which pads need to be filled
	for (uint8_t x = 0; x < GridWidth; x++) {
		for (uint8_t y = 0; y < GridHeight; y++) {
			if (!fieldFilled(x, y)) {
				goPadsToFill.emplace_back(std::pair(x, y));
			}
		}
	}
	goTotalPadsToFill = goPadsToFill.size();
	// Start game over animation
	gameState = GameState::GameOverAnimation;
	TK::log(goStartTime);
}

void TetrisView::gameOverAnimation() {
	static constexpr uint32_t PadsFillTime = TK::toSamples(800);
	static constexpr uint32_t PadsWaitTime = TK::toSamples(200);
	static constexpr uint32_t PadsClearTime = TK::toSamples(1000);
	static constexpr uint32_t ClearTimePerRow = PadsClearTime / (GridHeight + 2);
	static constexpr RGB PadsFillColor = RGB(64, 0, 8);
	static uint8_t clearingRow;
	static uint32_t lastClearRowSwitch;

	// Animation done
	if (TK::timeSince(goStartTime) >= PadsFillTime + PadsClearTime + 2 * PadsWaitTime) {
		initGame();
		return;
	}
	// Wait stage 2
	if (TK::timeSince(goStartTime) >= PadsFillTime + PadsWaitTime + PadsClearTime) {
		return;
	}
	// Clear stage
	if (TK::timeSince(goStartTime) >= PadsFillTime + PadsWaitTime) {
		float rowProgress = min(TK::timeSince(lastClearRowSwitch) * 1.0 / ClearTimePerRow, 1);
		for (uint8_t x = 0; x < GridWidth; x++) {
			RGB sourceColor = fieldFilled(x, clearingRow) ? fieldColor[x][clearingRow] : PadsFillColor;
			// fade current row from source color to white
			drawColor(x, clearingRow,
			          RGB(lerp(sourceColor.r, 255, rowProgress), lerp(sourceColor.g, 255, rowProgress),
			              lerp(sourceColor.b, 255, rowProgress)));
			// fade next row from white to off
			drawColor(x, clearingRow + 1, RGB::monochrome((1 - rowProgress) * 255));
		}
		// row is handled
		if (TK::timeSince(lastClearRowSwitch) >= ClearTimePerRow) {
			// move to next row
			lastClearRowSwitch += ClearTimePerRow;
			clearingRow--;
		}
		return;
	}
	// Wait stage 1
	if (TK::timeSince(goStartTime) >= PadsFillTime) {
		clearingRow = GridHeight - 1;
		TK::log(lastClearRowSwitch);
		return;
	}
	// Fill stage: does a pad need to be filled?
	if (TK::timeSince(goStartTime) >= PadsFillTime * (goTotalPadsToFill - goPadsToFill.size()) / goTotalPadsToFill) {
		// Pick a random pad
		uint8_t padId = TK::random(goPadsToFill.size());
		// Fill it
		drawColor(goPadsToFill[padId].first, goPadsToFill[padId].second, PadsFillColor);
		// Delete it from the list
		goPadsToFill.erase(goPadsToFill.begin() + padId);
	}
}

} // namespace bareluge
