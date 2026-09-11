// Chess game for ESP32-2432S028R with ILI9341 TFT display
//
// Started from Schematik's "Touchscreen Chess Game on a Cheap Yellow
// Display" guide (schematik.io/guides/esp32/build-a-touchscreen-chess-game-on-a-cheap-yellow-display)
// -- the touch/TFT pin wiring and the overall setup()/loop() structure
// trace back to that base sketch; the engine, pieces, rules, menu, undo,
// and persistence have all since been replaced or added (each credited in
// its own file). See larsi.org/make/cyd-chess/ for the full write-up.
//
// Player (White or Black, chosen from the MENU) vs AI -- rules/board/move
// generation (chess_rules.h/.cpp), H.G. Muller's micro-Max engine
// (micromax.h/.cpp), a small opening book (book.h/.cpp), and piece graphics
// (pieces.h) all split into their own files; this .ino is display/touch/
// setup/loop -- the UI wrapped around them
// Touch screen for piece selection and movement, one-level undo

#include <SPI.h>

// ESP32-2432S028R integrated ILI9341 wiring: pins, HSPI-vs-VSPI host, and
// panel geometry all come from the *machine's installed* TFT_eSPI/User_Setup.h
// (~/Arduino/libraries/TFT_eSPI/User_Setup.h on this dev box), not a
// sketch-local override. TFT_eSPI.cpp is a separate translation unit
// compiled once as a library — a `#define USER_SETUP_LOADED` block here only
// changes what *this .ino* sees, not what the already-compiled driver code
// was built against, so a sketch-local redefine here silently desyncs the
// two and tft.init() hangs (WDT reset) despite compiling cleanly. This
// board's installed User_Setup.h already targets this exact CYD panel
// (pins 12/13/14/15/2, HSPI, ILI9341, BGR order) and reports 240x320 at
// rotation(0), matching this sketch's portrait board layout as-is.
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include "chess_rules.h"
#include "pieces.h"
#include "book.h"
#include "micromax.h"
#include "persistence.h"
#include "undo.h"
#include "menu.h"

// Override loopTask stack size to 64KB. arduino-esp32 v2.0.17 declares
// `size_t getArduinoLoopTaskStackSize(void);` with C++ linkage in Arduino.h
// (not extern "C"), so our override must match that linkage exactly.
size_t getArduinoLoopTaskStackSize() {
  return 64 * 1024;
}

// ─── Pin Definitions ───────────────────────────────────────────────────────
#define TFT_BL_PIN    21
#define TOUCH_CS_PIN  33
#define TOUCH_IRQ_PIN 36
#define TOUCH_MOSI    32
#define TOUCH_MISO    39
#define TOUCH_SCK     25

// Onboard RGB status LED -- unused by this sketch, but left floating it reads as a dim glow
// (active-low, so an undriven pin partially conducts) rather than fully off.
#define LED_R_PIN 4
#define LED_G_PIN 16
#define LED_B_PIN 17

// ─── Display & Touch Objects ───────────────────────────────────────────────

// Forward declarations (chess rules themselves are declared by
// chess_rules.h, included above)
void drawStatus(const char *msg);
void drawGameOver(const char *msg);
void drawPiece(int row, int col, int piece, int pieceColor);
void drawSquare(int row, int col, bool highlight, bool moveDot, GameState &gs);
void drawBoard(GameState &gs);
void redrawSquare(GameState &gs, int r, int c);
void redrawSelectionChange(GameState &gs, int oldSelRow, int oldSelCol, bool oldDotSquare[8][8]);
void getTouchSquare(int &row, int &col);

TFT_eSPI tft = TFT_eSPI();
SPIClass touchSPI(VSPI);
XPT2046_Touchscreen touch(TOUCH_CS_PIN, TOUCH_IRQ_PIN);

// ─── Chess Constants ───────────────────────────────────────────────────────
// Piece type (PAWN..KING) and color (WHITE_PIECE/BLACK_PIECE) constants now
// come from chess_rules.h.
#define SQUARE_SIZE   30
#define BOARD_OFFSET_X 0
#define BOARD_OFFSET_Y 0

// Layout, top to bottom: board (y=0..240), file/rank coordinate labels
// (y=242..250ish), status line, then the Menu/Undo button row (unchanged
// at the very bottom -- see menu.cpp's BTN_Y comment). Every drawStatus()
// call is a single short line (never wraps), so the box only needs to be
// tall enough for that one line -- 38px was sized back when this was a
// standalone bar at the very top of the screen; sandwiched between the
// board and the buttons now, that height left a lot of visibly empty
// status-colored space below the text.
#define STATUS_Y 260
#define STATUS_H 16

// TFT Colors
// Light/dark square colors match WinBoard/XBoard's own classic default
// theme (lightSquareColor #C8C365, darkSquareColor #77A26D) -- yellow
// gave white pieces/outlines too little contrast against it.
#define COLOR_LIGHT_SQ  0xCE0C
#define COLOR_DARK_SQ   0x750D
#define COLOR_SELECTED  0x07E0
#define COLOR_MOVE_DOT  0x07FF
#define COLOR_BG        0x0000
#define COLOR_WHITE_P   0xFFFF
#define COLOR_BLACK_P   0x18C3
#define COLOR_TEXT      0xFFFF
#define COLOR_STATUS_BG 0x2104

// ─── Globals ───────────────────────────────────────────────────────────────
GameState gs;
int selectedRow = -1;
int selectedCol = -1;
bool pieceSelected = false;
Move legalMoves[256];
int legalMoveCount = 0;
bool gameOver = false;
int humanColor = WHITE_PIECE; // which side the player is -- MENU sets this, any time
bool inMenu = false;          // showing the menu screen, board hidden

// MENU's DIFFICULTY screen -- a standalone, always-live setting (like
// color), not tied to starting a new game. Default Expert (index 3) so a
// fresh boot, before anyone's touched the menu, plays exactly like this
// sketch did before this feature existed.
int aiStrength = 3; // 0=Easy 1=Medium 2=Hard 3=Expert
bool inDifficultyMenu = false;

// Node budget: see mmNodeBudget's own comment in micromax.cpp -- lower means
// a shallower/faster (weaker) search, same knob that already controls the
// ~5s-per-move Expert default. Blunder %: once out of book, the chance per
// AI move that a uniformly random legal move gets played instead of the
// engine's actual choice (see the AI-turn branch in loop()) -- 0 at Expert
// means it plays exactly as before this feature existed.
const int STRENGTH_NODE_BUDGET[4] = {500, 2000, 8000, 30000};
const int STRENGTH_BLUNDER_PCT[4] = {20, 8, 2, 0};

// MENU's PIECE SET screen -- another standalone, always-live setting (see
// pieces.h for what each index selects). Default 0 (Classic) so a fresh
// boot looks exactly like it did before this feature existed.
int pieceSet = 0;
bool inPieceSetMenu = false;

// Set when the human's tapped move reaches the back rank -- the move
// itself is held here, promoteTo unset, until the promotion-choice screen
// picks a piece and completeHumanMove() actually applies it.
bool awaitingPromotion = false;
Move pendingPromotionMove;

// Move-rating: bestScore captured (from microMaxConsumeBackgroundEval()) the
// moment the human's move is committed -- what micro-Max itself would have
// scored the best move from that same position. Compared against the AI's
// own reply-search score once one exists (see haveReplyScore in the AI-turn
// branch) to show a rough good/OK/inaccurate toast. Cleared on anything that
// makes the pending rating stale before that comparison happens -- a new
// game, a side switch, or an undo.
int pendingMoveRatingBestScore = 0;
bool havePendingMoveRating = false;

// How many moves the current human has made this game -- gates the rating toast (see
// RATING_STARTS_AT_MOVE below) rather than showing one on the opening moves, where a shallow
// search's small score gaps between perfectly reasonable choices aren't a meaningful "mistake".
// Neither existing counter fits this: gs.halfmoveClock resets on every pawn move/capture (it's
// only for the 50-move rule), and book.cpp's plyCount counts *both* sides' plies for opening-book
// tracking -- reusing it here would tie this feature to the book's own bookkeeping. Incremented
// in completeHumanMove(), reset in resetGame(), and included in undo.cpp's snapshot so Undo rolls
// it back too (same treatment plyCount already gets, for the same reason).
int humanMoveCount = 0;
const int RATING_STARTS_AT_MOVE = 3;

// True when the human picked Black -- the board is drawn (and touch input
// read) rotated 180 so the human's own pieces are nearest the bottom,
// matching how a real board looks from either side of the table. Computed
// fresh from humanColor rather than cached, so there's no separate flag to
// keep in sync.
bool boardFlipped() {
  return humanColor == BLACK_PIECE;
}

// Touch calibration values (may need tuning)
#define TOUCH_X_MIN 200
#define TOUCH_X_MAX 3800
#define TOUCH_Y_MIN 300
#define TOUCH_Y_MAX 3700

// ─── Drawing Functions ─────────────────────────────────────────────────────
void drawStatus(const char *msg) {
  tft.fillRect(0, STATUS_Y, 240, STATUS_H, COLOR_STATUS_BG);
  tft.setTextColor(COLOR_TEXT, COLOR_STATUS_BG);
  tft.setTextSize(1);
  tft.setCursor(4, STATUS_Y + 4);
  tft.print(msg);
}

// Move-rating's own line, directly below the main status line -- kept separate so a rating isn't
// sharing a line drawStatus() is about to overwrite with "Your turn"/"AI thinking" right after it
// (it used to, and did). Persists until the next rating replaces it or something clears it (New
// Game, a side switch, or an undo -- see those call sites) rather than on a timer, so there's no
// need to block the game loop with a delay just to give it time to be read. RATING_Y/_H reuse
// STATUS_Y/_H's own sizing one line down; the resulting 2px gap above BTN_Y (294) is deliberate,
// not a rounding accident.
#define RATING_Y (STATUS_Y + STATUS_H)
#define RATING_H STATUS_H

void drawRatingLine(const char *msg) {
  tft.fillRect(0, RATING_Y, 240, RATING_H, COLOR_STATUS_BG);
  tft.setTextColor(COLOR_TEXT, COLOR_STATUS_BG);
  tft.setTextSize(1);
  tft.setCursor(4, RATING_Y + 4);
  tft.print(msg);
}

// Collapses every "Your turn (White)"/"Your turn (Black)"/"AI thinking..."
// site into one place -- correct regardless of which color is human since
// it re-checks gs.currentPlayer/humanColor fresh each call. Also the one
// place that starts the move-rating background eval (see its own comment in
// micromax.h) -- every "it's now the human's turn" transition in this sketch
// (a fresh game, an undo, a side switch, or the AI's own move completing)
// already funnels through here, so this is the only trigger point needed.
void showTurnStatus() {
  if (gs.currentPlayer == humanColor) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Your turn (%s)", humanColor == WHITE_PIECE ? "White" : "Black");
    drawStatus(buf);
    microMaxStartBackgroundEval();
  } else {
    drawStatus("AI thinking...");
  }
}

// Rough good/OK/inaccurate bucketing for a move-rating "loss" -- the gap
// between what micro-Max's own search would have scored as the best move
// from a position, and what the human's actual move left it scored at (see
// the AI-turn branch's use of this). Units are micro-Max's own internal
// score scale, not centipawns -- captures run roughly 40-300+ units
// depending on the piece taken (mmW[]'s piece-value comment in micromax.cpp
// has the per-piece scale; the `37x` multiplier applying it is in mmD()'s
// capture-scoring line).
//
// Calibrated off-device (not just a guess): self-played several thousand
// moves across all four difficulty levels, comparing three move-quality
// tiers (the engine's own choice, a "take the best capture / give check"
// heuristic, and a uniformly random legal move) against the same loss
// formula used here. The engine's own choice clustered at 0 with a noise
// tail out to ~250-300 (inherent to comparing two independent searches --
// even the objectively same move doesn't always score identically twice);
// the heuristic and random tiers spread further, with random's tail
// reaching well past 1000 on genuine blunders. 150/400 sits so the engine's
// own noise tail lands mostly in Good, with real mistakes needing to
// clearly exceed it to read as OK, and only the heavier blunder tail as Not
// the best.
const char *rateMoveLoss(int loss) {
  if (loss <= 150) return "Good move!";
  if (loss <= 400) return "OK move";
  return "Not the best";
}

// Remembered so a later redraw (e.g. returning from the MENU after switching
// sides right after a checkmate/stalemate) can restore the same banner.
const char *lastGameOverMsg = "";

void drawGameOver(const char *msg) {
  lastGameOverMsg = msg;
  tft.fillRect(20, 100, 200, 60, TFT_RED);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.setTextSize(2);
  tft.setCursor(30, 115);
  tft.print(msg);
  tft.setTextSize(1);
  tft.setCursor(44, 140);
  tft.print("Tap MENU to play again");
}

void drawPieceBitmap(int x, int y, const uint8_t *bitmap, uint16_t color) {
  const int byteWidth = (PIECE_SIZE + 7) / 8;
  for (int j = 0; j < PIECE_SIZE; j++) {
    for (int i = 0; i < PIECE_SIZE; i++) {
      if (pgm_read_byte(bitmap + j * byteWidth + i / 8) & (128 >> (i & 7))) {
        tft.drawPixel(x + i, y + j, color);
      }
    }
  }
}

// Top-left pixel of board square (row,col), accounting for boardFlipped() --
// the one place the 180-degree rotation for a Black-playing human actually
// happens. Everything else (selection state, move generation, the rules
// layer) stays entirely in board coordinates; only pixel placement rotates.
void boardToScreenXY(int row, int col, int &x, int &y) {
  int screenRow = boardFlipped() ? 7 - row : row;
  int screenCol = boardFlipped() ? 7 - col : col;
  x = BOARD_OFFSET_X + screenCol * SQUARE_SIZE;
  y = BOARD_OFFSET_Y + screenRow * SQUARE_SIZE;
}

void drawPiece(int row, int col, int piece, int pieceColor) {
  int x, y;
  boardToScreenXY(row, col, x, y);
  x += (SQUARE_SIZE - PIECE_SIZE) / 2;
  y += (SQUARE_SIZE - PIECE_SIZE) / 2;

  int idx = piece - 1; // PAWN=1..KING=6 -> 0..5
  if (idx < 0 || idx > 5) return;

  uint16_t fg = (pieceColor == WHITE_PIECE) ? COLOR_WHITE_P : COLOR_BLACK_P;
  uint16_t outline = (pieceColor == WHITE_PIECE) ? TFT_BLACK : TFT_WHITE;

  drawPieceBitmap(x, y, PIECE_FILL_SETS[pieceSet][idx], fg);
  drawPieceBitmap(x, y, PIECE_OUTLINE_SETS[pieceSet][idx], outline);
}

void drawSquare(int row, int col, bool highlight, bool moveDot, GameState &gs) {
  int x, y;
  boardToScreenXY(row, col, x, y);
  uint16_t bg;

  if (highlight) {
    bg = COLOR_SELECTED;
  } else {
    // Unflipped (row+col) on purpose -- a real board's square colors don't
    // change when you walk around the table, only your view of them does.
    bg = ((row + col) % 2 == 0) ? COLOR_LIGHT_SQ : COLOR_DARK_SQ;
  }

  tft.fillRect(x, y, SQUARE_SIZE, SQUARE_SIZE, bg);

  if (moveDot && gs.board[row][col] == EMPTY) {
    tft.fillCircle(x + SQUARE_SIZE / 2, y + SQUARE_SIZE / 2, 4, COLOR_MOVE_DOT);
  } else if (moveDot && gs.board[row][col] != EMPTY) {
    // Highlight capture
    tft.drawRect(x, y, SQUARE_SIZE, SQUARE_SIZE, COLOR_MOVE_DOT);
    tft.drawRect(x + 1, y + 1, SQUARE_SIZE - 2, SQUARE_SIZE - 2, COLOR_MOVE_DOT);
  }

  if (gs.board[row][col] != EMPTY) {
    drawPiece(row, col, gs.board[row][col], gs.color[row][col]);
  }
}

// Changes which color the human plays *without* resetting the game -- supports
// switching sides mid-game (a teaching technique: "finish what the other
// person started"). If it's currently the new AI color's turn, the AI just
// picks up from here on the very next loop() iteration -- no special-casing
// needed, the same mechanism that already makes "AI moves first" work when
// starting a new game as Black.
void switchHumanColor(int newColor) {
  humanColor = newColor;
  // No coherent "my last move" survives a side-swap -- it may have been
  // made by a different color's human -- so don't offer to undo across one.
  invalidateUndoSnapshot();
  havePendingMoveRating = false; // same reasoning -- stale, refers to the old color's move
  humanMoveCount = 0; // fresh grace period -- a new human (color-wise) is now making decisions
  pieceSelected = false;
  selectedRow = -1;
  selectedCol = -1;
  legalMoveCount = 0;
  // Defensive, not currently reachable -- loop()'s awaitingPromotion check
  // already blocks every other path (including this one) until a pending
  // promotion is resolved. Cleared anyway so this stays true even if that
  // ordering ever changes.
  awaitingPromotion = false;
  tft.fillScreen(COLOR_BG);
  drawBoard(gs); // re-renders flipped/unflipped per the new boardFlipped()
  drawMenuButton();
  drawUndoButton();
  if (gameOver) drawGameOver(lastGameOverMsg);
  showTurnStatus();
  // Settings are their own tiny record, independent of whether a game is
  // in progress -- always cheap to keep current, unconditionally.
  saveSettings(humanColor, aiStrength, pieceSet);
}

// Changes the AI's strength *without* resetting the game -- unlike a color
// swap, this touches nothing about gs/mmB/book/undo at all (mmNodeBudget
// and STRENGTH_BLUNDER_PCT are both read fresh at the moment the AI decides
// its next move, never baked into any saved state), so there's nothing to
// invalidate or resync -- it just takes effect on the AI's next move.
void switchAiStrength(int newStrength) {
  aiStrength = newStrength;
  mmNodeBudget = STRENGTH_NODE_BUDGET[aiStrength];
  tft.fillScreen(COLOR_BG);
  drawBoard(gs);
  drawMenuButton();
  drawUndoButton();
  if (gameOver) drawGameOver(lastGameOverMsg);
  showTurnStatus();
  saveSettings(humanColor, aiStrength, pieceSet); // see switchHumanColor()'s comment
}

// Changes which piece bitmap set drawPiece() reads -- same standalone,
// always-live treatment as color/difficulty: no engine/board state to
// resync, just a redraw with the new set and a settings save.
void switchPieceSet(int newSet) {
  pieceSet = newSet;
  tft.fillScreen(COLOR_BG);
  drawBoard(gs);
  drawMenuButton();
  drawUndoButton();
  if (gameOver) drawGameOver(lastGameOverMsg);
  showTurnStatus();
  saveSettings(humanColor, aiStrength, pieceSet);
}

void resetGame() {
  initBoard(gs);
  microMaxInit();
  mmNodeBudget = STRENGTH_NODE_BUDGET[aiStrength];
  bookReset();
  resetPositionHistory();
  recordPosition(gs); // the starting position itself counts as its own first occurrence
  invalidateUndoSnapshot();
  havePendingMoveRating = false; // stale -- refers to a move from the previous game
  humanMoveCount = 0; // fresh grace period for the new game
  pieceSelected = false;
  selectedRow = -1;
  selectedCol = -1;
  legalMoveCount = 0;
  gameOver = false;
  // Defensive, not currently reachable -- see the matching comment in
  // switchHumanColor().
  awaitingPromotion = false;
  tft.fillScreen(COLOR_BG);
  drawBoard(gs);
  drawMenuButton();
  drawUndoButton();
  showTurnStatus();
  // Nothing worth resuming yet at the bare starting position -- clear
  // whatever a previous game left behind rather than writing this one out
  // (it'll get saved for real the moment an actual move is made).
  clearPersistedGame();
}

void drawBoard(GameState &gs) {
  // No whole-board clear here -- every square below gets its own fillRect
  // in drawSquare() regardless, so a prior full-board fill only added an
  // extra flash of solid color before every single redraw.

  // Determine if any square is selected and build move dots
  bool dotSquare[8][8];
  memset(dotSquare, false, sizeof(dotSquare));

  if (pieceSelected) {
    for (int i = 0; i < legalMoveCount; i++) {
      dotSquare[legalMoves[i].toRow][legalMoves[i].toCol] = true;
    }
  }

  for (int r = 0; r < 8; r++) {
    for (int c = 0; c < 8; c++) {
      bool highlight = (pieceSelected && r == selectedRow && c == selectedCol);
      drawSquare(r, c, highlight, dotSquare[r][c], gs);
    }
  }

  // Draw coordinates -- i is a *screen* position (0-7, left-to-right /
  // top-to-bottom); the board file/rank actually shown there depends on
  // boardFlipped(), same transform as boardToScreenXY() but run in reverse.
  //
  // File letters sit in the dedicated strip below the board (on the plain
  // black background there, so a bright color reads cleanly).
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, COLOR_BG);
  for (int i = 0; i < 8; i++) {
    int boardCol = boardFlipped() ? 7 - i : i;
    tft.setCursor(BOARD_OFFSET_X + i * SQUARE_SIZE + 12, BOARD_OFFSET_Y + 8 * SQUARE_SIZE + 2);
    tft.print((char)('a' + boardCol));
  }

  // Rank numbers have no equivalent margin to draw in -- the board already
  // fills the screen's full 240px width, so they're drawn overlaid in the
  // bottom-left corner of column 0's squares instead (standard chess
  // convention puts rank numbers on the left edge anyway). Ink color is
  // chosen per-square, since one fixed color can't read well against both
  // the light and dark square colors; background matches the square color
  // itself so the label blends in rather than sitting in a mismatched box.
  // (Screen row i, column 0's square is light exactly when i is even --
  // true regardless of boardFlipped(), since flipping negates both row and
  // col together and 8 is even.)
  for (int i = 0; i < 8; i++) {
    int boardRow = boardFlipped() ? 7 - i : i;
    bool sqIsLight = (i % 2 == 0);
    tft.setTextColor(sqIsLight ? TFT_BLACK : TFT_WHITE, sqIsLight ? COLOR_LIGHT_SQ : COLOR_DARK_SQ);
    tft.setCursor(BOARD_OFFSET_X + 2, BOARD_OFFSET_Y + i * SQUARE_SIZE + SQUARE_SIZE - 9);
    tft.print(8 - boardRow);
  }
}

// Redraws a single square using the *current* global selection state
// (pieceSelected/selectedRow/selectedCol/legalMoves) -- the same
// highlight/move-dot logic drawBoard() computes per-square, just for one
// square instead of all 64.
void redrawSquare(GameState &gs, int r, int c) {
  bool highlight = (pieceSelected && r == selectedRow && c == selectedCol);
  bool moveDot = false;
  for (int i = 0; i < legalMoveCount; i++) {
    if (legalMoves[i].toRow == r && legalMoves[i].toCol == c) { moveDot = true; break; }
  }
  drawSquare(r, c, highlight, moveDot, gs);
}

// Redraws only the squares whose highlight/move-dot state can actually have
// changed from a pure selection change (select/deselect/re-select/invalid-
// move-cleared) -- none of these change gs.board itself, so repainting all
// 64 squares (drawBoard()'s job, needed when a move is actually applied) is
// needless work that shows up as a visible flicker on every tap, including
// on squares whose piece never changed. Takes the selection state from
// *before* the change since that's already been overwritten by the time
// this is called; the *current* globals supply the "after" state.
void redrawSelectionChange(GameState &gs, int oldSelRow, int oldSelCol, bool oldDotSquare[8][8]) {
  bool touched[8][8];
  memset(touched, false, sizeof(touched));

  if (oldSelRow >= 0) touched[oldSelRow][oldSelCol] = true;
  for (int r = 0; r < 8; r++)
    for (int c = 0; c < 8; c++)
      if (oldDotSquare[r][c]) touched[r][c] = true;

  if (pieceSelected) touched[selectedRow][selectedCol] = true;
  for (int i = 0; i < legalMoveCount; i++) touched[legalMoves[i].toRow][legalMoves[i].toCol] = true;

  for (int r = 0; r < 8; r++)
    for (int c = 0; c < 8; c++)
      if (touched[r][c]) redrawSquare(gs, r, c);
}

// ─── Touch Input ───────────────────────────────────────────────────────────
void getTouchSquare(int &row, int &col) {
  row = -1; col = -1;
  if (!touch.touched()) return;

  TS_Point p = touch.getPoint();

  // Map raw touch to screen coordinates
  // The display is 240x320, touch is 240 wide x 320 tall
  // Raw values need mapping based on calibration
  int tx = map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, 240);
  int ty = map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, 320);

  tx = constrain(tx, 0, 239);
  ty = constrain(ty, 0, 319);

  // Convert to board square
  int bx = tx - BOARD_OFFSET_X;
  int by = ty - BOARD_OFFSET_Y;

  if (bx < 0 || bx >= 8 * SQUARE_SIZE) return;
  if (by < 0 || by >= 8 * SQUARE_SIZE) return;

  col = bx / SQUARE_SIZE;
  row = by / SQUARE_SIZE;

  // Self-inverse: the same flip used to place squares/pieces on screen
  // undoes itself here to recover the underlying board coordinate.
  if (boardFlipped()) {
    row = 7 - row;
    col = 7 - col;
  }
}

// Ends the game: status line, the red game-over banner, marks gameOver,
// and clears the persisted game (below) -- a finished game has nothing
// left to resume, and by this point a save from just before this move
// completed is already sitting in flash claiming otherwise. Shared by
// both completeHumanMove() and the AI-turn branch below -- was six
// near-identical copies of this same triple before.
void endGame(const char *statusMsg, const char *bannerMsg) {
  drawStatus(statusMsg);
  drawGameOver(bannerMsg);
  gameOver = true;
  clearPersistedGame();
}

// Finishes applying a human move already confirmed legal -- and, for a
// promotion, already resolved to a specific piece via the promotion-choice
// screen. Shared by the ordinary (non-promotion) move-completion path and
// the promotion-choice resume path in loop(), so the save-undo/apply/sync/
// redraw/game-end-check sequence lives in exactly one place.
void completeHumanMove(Move &m) {
  // Consume whatever the background eval (started the moment this turn began -- see
  // showTurnStatus()) found for this position, before anything below changes it. Blocks only if
  // the human moved faster than the search finished.
  havePendingMoveRating = microMaxConsumeBackgroundEval(pendingMoveRatingBestScore);

  saveUndoSnapshot(gs); // captures the position as it stood right before this move
  humanMoveCount++;
  applyMove(gs, m);
  recordPosition(gs);
  savePersistedGame(gs);
  microMaxApplyMove(m.fromRow, m.fromCol, m.toRow, m.toCol);
  bookRecordMove(m.fromRow, m.fromCol, m.toRow, m.toCol);

  drawBoard(gs);

  if (isCheckmate(gs, -humanColor)) {
    endGame("Checkmate! You win!", "You Win!");
    return;
  }
  if (isStalemate(gs, -humanColor)) {
    endGame("Stalemate!", "Stalemate!");
    return;
  }
  if (isDraw(gs)) {
    endGame("Draw!", "Draw!");
    return;
  }
  if (isInCheck(gs, -humanColor)) {
    drawStatus("Check! AI thinking...");
  } else {
    drawStatus("AI thinking...");
  }
}

// ─── Setup & Loop ──────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // Backlight
  ledcAttach(TFT_BL_PIN, 5000, 8);
  ledcWrite(TFT_BL_PIN, 200);

  pinMode(LED_R_PIN, OUTPUT);
  pinMode(LED_G_PIN, OUTPUT);
  pinMode(LED_B_PIN, OUTPUT);
  digitalWrite(LED_R_PIN, HIGH);
  digitalWrite(LED_G_PIN, HIGH);
  digitalWrite(LED_B_PIN, HIGH);

  // TFT
  tft.init();
  tft.setRotation(0); // Portrait
  tft.fillScreen(COLOR_BG);

  // Touch SPI
  touchSPI.begin(TOUCH_SCK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS_PIN);
  touch.begin(touchSPI);
  touch.setRotation(0);

  // Settings (color/difficulty) are independent of whether a game is in
  // progress -- load them first, keeping the compiled-in defaults if
  // nothing's ever been saved.
  loadSettings(humanColor, aiStrength, pieceSet);

  // Resume a persisted in-progress game if there is one (power cycled
  // mid-game), otherwise start fresh. loadPersistedGame() fully populates
  // gs and the engine/book/position-history globals directly when it
  // succeeds, so none of the usual fresh-game init calls run in that case
  // -- microMaxInit() in particular would stomp the just-restored mmB
  // back to a fresh board.
  if (loadPersistedGame(gs)) {
    mmNodeBudget = STRENGTH_NODE_BUDGET[aiStrength]; // not part of the saved blob
    invalidateUndoSnapshot(); // Undo's single RAM-only snapshot never survives a reboot
    gameOver = false; // a persisted game is only ever saved while still in progress
  } else {
    initBoard(gs);
    microMaxInit();
    mmNodeBudget = STRENGTH_NODE_BUDGET[aiStrength];
    bookReset();
    resetPositionHistory();
    recordPosition(gs); // the starting position itself counts as its own first occurrence
    invalidateUndoSnapshot();
    // Nothing worth persisting yet at the bare starting position -- see
    // resetGame()'s matching comment.
  }

  tft.fillScreen(COLOR_BG);
  drawBoard(gs);
  drawMenuButton();
  drawUndoButton();
  showTurnStatus();

  // Diagnostic: how much loopTask stack is left at the end of setup()?
  // Reports the minimum free stack (high-water mark). If this is small,
  // setup() ate most of the stack and we need to bump getArduinoLoopTaskStackSize.
  Serial.printf("[diag] loopTask stack high-water mark: %u bytes free\n",
                (unsigned)uxTaskGetStackHighWaterMark(NULL));
  Serial.println("Chess game started");
}

void loop() {
  // ESP32 task watchdog feed: arduino-esp32 v2.x's loopTask doesn't yield
  // between iterations, so a busy-waiting `loop()` (no touch → return early)
  // starves Core 1 IDLE within ~5s and trips TG0WDT_SYS_RESET. delay(1)
  // sleeps the loopTask for 1 tick, which is enough for IDLE to run.
  // (yield() does NOT fix this — it only schedules equal-or-higher priority.)
  delay(1);

  // Menu screen: covers the whole screen, so it's checked before (and
  // instead of) the Menu/Undo buttons below, which aren't visible right now.
  // The color toggle and DIFFICULTY/PIECE SET all apply immediately (no
  // board reset, work mid-game); NEW GAME starts a fresh game using whatever
  // color/difficulty/piece set are currently set, rather than asking again.
  if (inMenu) {
    if (isTouchOnMenuNewGameButton()) {
      delay(50); // debounce
      if (isTouchOnMenuNewGameButton()) {
        unsigned long _waitStart = millis();
        while (touch.touched() && millis() - _waitStart < 2000) { delay(10); }
        inMenu = false;
        resetGame();
      }
      return;
    }
    if (isTouchOnMenuColorButton()) {
      delay(50); // debounce
      if (isTouchOnMenuColorButton()) {
        unsigned long _waitStart = millis();
        while (touch.touched() && millis() - _waitStart < 2000) { delay(10); }
        inMenu = false;
        switchHumanColor(humanColor == WHITE_PIECE ? BLACK_PIECE : WHITE_PIECE);
      }
      return;
    }
    if (isTouchOnMenuDifficultyButton()) {
      delay(50); // debounce
      if (isTouchOnMenuDifficultyButton()) {
        unsigned long _waitStart = millis();
        while (touch.touched() && millis() - _waitStart < 2000) { delay(10); }
        inMenu = false;
        inDifficultyMenu = true;
        drawDifficultyMenu();
      }
      return;
    }
    if (isTouchOnMenuPieceSetButton()) {
      delay(50); // debounce
      if (isTouchOnMenuPieceSetButton()) {
        unsigned long _waitStart = millis();
        while (touch.touched() && millis() - _waitStart < 2000) { delay(10); }
        inMenu = false;
        inPieceSetMenu = true;
        drawPieceSetMenu();
      }
      return;
    }
    return;
  }

  // Difficulty menu: MENU's DIFFICULTY button leads here. Picking a level
  // applies immediately (switchAiStrength()) -- same standalone, always-live
  // treatment as color, not tied to starting a new game.
  if (inDifficultyMenu) {
    for (int i = 0; i < 4; i++) {
      if (isTouchOnDifficultyButton(i)) {
        delay(50); // debounce
        if (isTouchOnDifficultyButton(i)) {
          unsigned long _waitStart = millis();
          while (touch.touched() && millis() - _waitStart < 2000) { delay(10); }
          inDifficultyMenu = false;
          switchAiStrength(i);
        }
        return;
      }
    }
    return;
  }

  // Piece set menu: MENU's PIECE SET button leads here. Picking a set
  // applies immediately (switchPieceSet()), same standalone, always-live
  // treatment as color/difficulty.
  if (inPieceSetMenu) {
    for (int i = 0; i < NUM_PIECE_SETS; i++) {
      if (isTouchOnPieceSetButton(i)) {
        delay(50); // debounce
        if (isTouchOnPieceSetButton(i)) {
          unsigned long _waitStart = millis();
          while (touch.touched() && millis() - _waitStart < 2000) { delay(10); }
          inPieceSetMenu = false;
          switchPieceSet(i);
        }
        return;
      }
    }
    return;
  }

  // Promotion choice: the human's pawn move is already picked (stored in
  // pendingPromotionMove) and waiting on which piece to become -- nothing
  // else runs until this is resolved, same "modal" treatment as the menus
  // above.
  if (awaitingPromotion) {
    for (int i = 0; i < 4; i++) {
      if (isTouchOnPromotionButton(i)) {
        delay(50); // debounce
        if (isTouchOnPromotionButton(i)) {
          unsigned long _waitStart = millis();
          while (touch.touched() && millis() - _waitStart < 2000) { delay(10); }
          pendingPromotionMove.promoteTo = PROMO_PIECES[i];
          awaitingPromotion = false;
          completeHumanMove(pendingPromotionMove);
          // The promotion screen did a full fillScreen(), unlike an
          // ordinary move -- redraw the buttons it wiped.
          drawMenuButton();
          drawUndoButton();
        }
        return;
      }
    }
    return;
  }

  // Menu button: works in any game state (mid-game, game over, AI's turn).
  // Check before all other touch handling so a tap on the button always
  // wins. Wait for finger release with the same 2s timeout used elsewhere
  // so a stuck touch sensor can't freeze the transition.
  if (isTouchOnMenuButton()) {
    delay(50); // debounce
    if (isTouchOnMenuButton()) {
      unsigned long _waitStart = millis();
      while (touch.touched() && millis() - _waitStart < 2000) { delay(10); }
      inMenu = true;
      drawMenuScreen(humanColor, aiStrength, pieceSet);
      return;
    }
  }

  // Undo button: same "works in any game state" treatment as Menu,
  // including backing out of a just-delivered checkmate/stalemate so a
  // blunder doesn't have to end the game. Single level only -- restoring
  // the snapshot consumes it, so a second tap with nothing left to undo
  // just reports that rather than doing anything.
  if (isTouchOnUndoButton()) {
    delay(50); // debounce
    if (isTouchOnUndoButton()) {
      unsigned long _waitStart = millis();
      while (touch.touched() && millis() - _waitStart < 2000) { delay(10); }
      if (restoreUndoSnapshot(gs)) {
        gameOver = false;
        havePendingMoveRating = false; // stale -- refers to the just-undone move
        // Unlike resetGame()/switchHumanColor(), nothing below clears the whole screen -- the
        // rating line (see drawRatingLine()) needs an explicit blank or a stale rating stays put.
        drawRatingLine("");
        pieceSelected = false;
        selectedRow = -1;
        selectedCol = -1;
        legalMoveCount = 0;
        drawBoard(gs);
        showTurnStatus();
      } else {
        drawStatus("Nothing to undo!");
        delay(800);
        if (gameOver) drawStatus("Game over");
        else showTurnStatus();
      }
      return;
    }
  }

  // No tap-anywhere-to-restart -- MENU (checked above, still works here) is
  // the one way to start again, with color/difficulty chosen deliberately
  // rather than an accidental board tap wiping out a finished game.
  if (gameOver) return;

  if (gs.currentPlayer == humanColor) {
    // Human's turn
    if (!touch.touched()) return;

    delay(50); // debounce
    if (!touch.touched()) return;

    int tRow, tCol;
    getTouchSquare(tRow, tCol);

    // Wait for release — same 2s timeout as the gameOver branch above.
    unsigned long _waitStart = millis();
    while (touch.touched() && millis() - _waitStart < 2000) { delay(10); }

    if (tRow < 0 || tRow >= 8 || tCol < 0 || tCol >= 8) return;

    // Snapshot the selection-display state before any of the branches below
    // mutate it, so a pure selection change (select/deselect/re-select/
    // invalid-move-cleared -- none of which touch gs.board) can redraw just
    // the squares that actually changed instead of repainting all 64 the
    // way an applied move's full drawBoard() below still needs to.
    int oldSelRow = selectedRow, oldSelCol = selectedCol;
    bool oldDotSquare[8][8];
    memset(oldDotSquare, false, sizeof(oldDotSquare));
    for (int i = 0; i < legalMoveCount; i++) {
      oldDotSquare[legalMoves[i].toRow][legalMoves[i].toCol] = true;
    }

    if (!pieceSelected) {
      // Select a piece
      if (gs.board[tRow][tCol] != EMPTY && gs.color[tRow][tCol] == humanColor) {
        selectedRow = tRow;
        selectedCol = tCol;
        pieceSelected = true;
        // Generate legal moves for this piece
        Move allMoves[256];
        int allCount = 0;
        generateMoves(gs, humanColor, allMoves, allCount);
        legalMoveCount = 0;
        for (int i = 0; i < allCount; i++) {
          if (allMoves[i].fromRow == selectedRow && allMoves[i].fromCol == selectedCol) {
            legalMoves[legalMoveCount++] = allMoves[i];
          }
        }
        redrawSelectionChange(gs, oldSelRow, oldSelCol, oldDotSquare);
        drawStatus("Select destination");
      }
    } else {
      // Deselect if tapping same square
      if (tRow == selectedRow && tCol == selectedCol) {
        pieceSelected = false;
        selectedRow = -1;
        selectedCol = -1;
        legalMoveCount = 0;
        redrawSelectionChange(gs, oldSelRow, oldSelCol, oldDotSquare);
        showTurnStatus();
        return;
      }

      // Re-select another of the human's own pieces
      if (gs.board[tRow][tCol] != EMPTY && gs.color[tRow][tCol] == humanColor) {
        selectedRow = tRow;
        selectedCol = tCol;
        Move allMoves[256];
        int allCount = 0;
        generateMoves(gs, humanColor, allMoves, allCount);
        legalMoveCount = 0;
        for (int i = 0; i < allCount; i++) {
          if (allMoves[i].fromRow == selectedRow && allMoves[i].fromCol == selectedCol) {
            legalMoves[legalMoveCount++] = allMoves[i];
          }
        }
        redrawSelectionChange(gs, oldSelRow, oldSelCol, oldDotSquare);
        drawStatus("Select destination");
        return;
      }

      // Try to make a move
      bool moveMade = false;
      bool needsPromotionChoice = false;
      for (int i = 0; i < legalMoveCount; i++) {
        if (legalMoves[i].toRow == tRow && legalMoves[i].toCol == tCol) {
          if (legalMoves[i].promotion) {
            // Defer -- completeHumanMove() runs once the promotion-choice
            // screen below picks a piece, not immediately.
            pendingPromotionMove = legalMoves[i];
            needsPromotionChoice = true;
          } else {
            completeHumanMove(legalMoves[i]);
          }
          moveMade = true;
          break;
        }
      }

      pieceSelected = false;
      selectedRow = -1;
      selectedCol = -1;
      legalMoveCount = 0;

      if (!moveMade) {
        redrawSelectionChange(gs, oldSelRow, oldSelCol, oldDotSquare);
        drawStatus("Invalid move!");
        delay(800);
        showTurnStatus();
        return;
      }

      if (needsPromotionChoice) {
        awaitingPromotion = true;
        drawPromotionMenu();
        return;
      }
    }
  } else {
    // AI's turn
    delay(100);

    Move allMoves[256];
    int allCount = 0;
    generateMoves(gs, -humanColor, allMoves, allCount);

    // Trust the book/micro-Max only as far as this sketch's own legal-move
    // list confirms -- match the chosen from/to squares against a move this
    // sketch already knows is legal, so promotion/en-passant/castling are
    // always applied via this sketch's own (already-tested) logic rather
    // than needing to trust either source's board state directly.
    Move best = allMoves[0]; // emergency fallback if no match is ever found
    bool found = false;
    bool aiFallbackUsed = false; // surfaced on-screen below, not just to Serial
    // Only the real micro-Max search below produces a score -- a book or
    // blunder-difficulty move never runs one, so there's nothing to rate the
    // human's preceding move against on those turns. See the rating toast
    // near the end of this branch.
    int replyScore = 0;
    bool haveReplyScore = false;

    int bkFromRow, bkFromCol, bkToRow, bkToCol;
    bool fromBook = bookGetMove(bkFromRow, bkFromCol, bkToRow, bkToCol);
    if (fromBook) {
      for (int i = 0; i < allCount; i++) {
        if (allMoves[i].fromRow == bkFromRow && allMoves[i].fromCol == bkFromCol &&
            allMoves[i].toRow == bkToRow && allMoves[i].toCol == bkToCol) {
          best = allMoves[i];
          found = true;
          break;
        }
      }
      if (found) {
        // Book move wasn't searched by micro-Max, so its board never saw
        // it -- sync it in the same way this sketch syncs the player's own
        // moves, or the two engines desync from here on.
        microMaxApplyMove(best.fromRow, best.fromCol, best.toRow, best.toCol);
      } else {
        aiFallbackUsed = true;
        Serial.printf("[book] chose (%d,%d)->(%d,%d), not in this sketch's own legal moves -- falling back to micro-Max\n",
                      bkFromRow, bkFromCol, bkToRow, bkToCol);
      }
    }

    if (!found) {
      // Difficulty: below Expert, occasionally play a random legal move
      // instead of running the real search -- decided *before* calling
      // microMaxGetBestMove(), since that call commits its chosen move
      // into micro-Max's own board as a side effect of finding it, so
      // there's no cheap way to override its choice afterward. Skipping
      // the search entirely when blundering avoids ever needing to.
      bool blunder = allCount > 1 && STRENGTH_BLUNDER_PCT[aiStrength] > 0 &&
                     (int)(esp_random() % 100) < STRENGTH_BLUNDER_PCT[aiStrength];
      if (blunder) {
        best = allMoves[esp_random() % allCount];
        found = true;
        // Same generic "sync this externally-chosen move into micro-Max's
        // board" call the book path above already uses.
        microMaxApplyMove(best.fromRow, best.fromCol, best.toRow, best.toCol);
      } else {
        int mmFromRow, mmFromCol, mmToRow, mmToCol;
        microMaxGetBestMove(mmFromRow, mmFromCol, mmToRow, mmToCol, &replyScore);
        haveReplyScore = true;
        for (int i = 0; i < allCount; i++) {
          if (allMoves[i].fromRow == mmFromRow && allMoves[i].fromCol == mmFromCol &&
              allMoves[i].toRow == mmToRow && allMoves[i].toCol == mmToCol) {
            best = allMoves[i];
            found = true;
            break;
          }
        }
        if (!found) {
          aiFallbackUsed = true;
          Serial.printf("[micro-Max] chose (%d,%d)->(%d,%d), not in this sketch's own legal moves -- falling back\n",
                        mmFromRow, mmFromCol, mmToRow, mmToCol);
        }
      }
    }

    bookRecordMove(best.fromRow, best.fromCol, best.toRow, best.toCol);

    applyMove(gs, best);
    recordPosition(gs);
    savePersistedGame(gs);

    drawBoard(gs);

    // Rate the human's just-finished move against this reply search's own score, now that one
    // exists (see haveReplyScore's comment above -- a book or blunder-difficulty AI move never
    // runs a real search, so there's nothing to rate against on those turns). Skipped for the
    // human's first couple of moves regardless -- this early, a shallow search's small score
    // gaps between perfectly reasonable choices aren't a meaningful "mistake" to flag.
    if (havePendingMoveRating) {
      if (haveReplyScore && humanMoveCount >= RATING_STARTS_AT_MOVE) {
        // [cyd-chess fix] replyScore is already mmQ from the AI's own root call -- "value for
        // whoever's next to move after its move", i.e. the human's own perspective directly (see
        // microMaxGetBestMove()'s comment in micromax.cpp). No negation: an earlier version of
        // this line assumed replyScore needed flipping from the AI's side, which double-negated
        // it against pendingMoveRatingBestScore's own (correct) negation and silently produced a
        // wrong-signed loss for essentially every move -- caught by an off-device calibration
        // run, not on-device play.
        int actualScoreForHuman = replyScore;
        int loss = pendingMoveRatingBestScore - actualScoreForHuman;
        drawRatingLine(rateMoveLoss(loss));
      }
      havePendingMoveRating = false;
    }

    // Should never actually trigger -- both the book and micro-Max have been
    // tested extensively (200-move self-play, 2000-trial book sampling, all
    // clean) -- but if a proposed move ever *doesn't* match this sketch's own
    // legal-move list, the emergency move[0] fallback above still played
    // something, and that's worth surfacing on-screen, not just to Serial,
    // since nobody's watching a serial monitor during normal play.
    if (aiFallbackUsed) {
      drawStatus("Engine glitch - played fallback move");
      delay(1500);
    }

    if (isCheckmate(gs, humanColor)) {
      endGame("Checkmate! AI wins!", "AI Wins!");
      return;
    }
    if (isStalemate(gs, humanColor)) {
      endGame("Stalemate!", "Stalemate!");
      return;
    }
    if (isDraw(gs)) {
      endGame("Draw!", "Draw!");
      return;
    }
    if (isInCheck(gs, humanColor)) {
      drawStatus("You're in Check!");
    } else {
      showTurnStatus();
    }
  }
}
