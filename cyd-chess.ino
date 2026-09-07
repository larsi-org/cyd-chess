// Chess game for ESP32-2432S028R with ILI9341 TFT display
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
void invalidateUndoSnapshot();
void drawUndoButton();
void getTouchSquare(int &row, int &col);

TFT_eSPI tft = TFT_eSPI();
SPIClass touchSPI(VSPI);
XPT2046_Touchscreen touch(TOUCH_CS_PIN, TOUCH_IRQ_PIN);

// ─── Chess Constants ───────────────────────────────────────────────────────
// Piece type (PAWN..KING) and color (WHITE_PIECE/BLACK_PIECE) constants now
// come from chess_rules.h.
#define SQUARE_SIZE   30
#define BOARD_OFFSET_X 0
#define BOARD_OFFSET_Y 40

// TFT Colors
#define COLOR_LIGHT_SQ  0xFFE0
#define COLOR_DARK_SQ   0x6B4D
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

// MENU's difficulty screen, reached via its NEW GAME button. Default Expert
// (index 3) so a fresh boot, before anyone's touched the menu, plays exactly
// like this sketch did before this feature existed.
int aiStrength = 3; // 0=Easy 1=Medium 2=Hard 3=Expert
bool inDifficultyMenu = false;

const char *STRENGTH_NAMES[4] = {"EASY", "MEDIUM", "HARD", "EXPERT"};
// Node budget: see mmNodeBudget's own comment in micromax.cpp -- lower means
// a shallower/faster (weaker) search, same knob that already controls the
// ~5s-per-move Expert default. Blunder %: once out of book, the chance per
// AI move that a uniformly random legal move gets played instead of the
// engine's actual choice (see the AI-turn branch in loop()) -- 0 at Expert
// means it plays exactly as before this feature existed.
const int STRENGTH_NODE_BUDGET[4] = {500, 2000, 8000, 30000};
const int STRENGTH_BLUNDER_PCT[4] = {20, 8, 2, 0};

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
  tft.fillRect(0, 0, 240, 38, COLOR_STATUS_BG);
  tft.setTextColor(COLOR_TEXT, COLOR_STATUS_BG);
  tft.setTextSize(1);
  tft.setCursor(4, 4);
  tft.print(msg);
}

// Collapses every "Your turn (White)"/"Your turn (Black)"/"AI thinking..."
// site into one place -- correct regardless of which color is human since
// it re-checks gs.currentPlayer/humanColor fresh each call.
void showTurnStatus() {
  if (gs.currentPlayer == humanColor) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Your turn (%s)", humanColor == WHITE_PIECE ? "White" : "Black");
    drawStatus(buf);
  } else {
    drawStatus("AI thinking...");
  }
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

  drawPieceBitmap(x, y, PIECE_FILL[idx], fg);
  drawPieceBitmap(x, y, PIECE_OUTLINE[idx], outline);
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

// Undo / New Game buttons — bottom of screen, below the chess board.
// 240×320 portrait: board occupies y=40..280; column labels at y=282..290;
// buttons get the remaining strip y=294..318, split into two side by side
// (5px margins, 10px gap: 5+110+10+110+5 = 240).
#define UNDO_BTN_X     5
#define NEW_GAME_BTN_X 125
#define BTN_Y          294
#define BTN_W          110
#define BTN_H          24

void drawTextButton(int x, int y, int w, int h, const char *label, int labelLen,
                     uint16_t fillColor, uint16_t borderColor, uint16_t textColor = TFT_WHITE) {
  tft.fillRoundRect(x, y, w, h, 4, fillColor);
  tft.drawRoundRect(x, y, w, h, 4, borderColor);
  tft.setTextSize(1);
  tft.setTextColor(textColor, fillColor);
  // Centred-ish text — TFT_eSPI default font is ~6 pixels per character.
  tft.setCursor(x + (w - labelLen * 6) / 2, y + (h - 8) / 2);
  tft.print(label);
}

// Was "NEW GAME" -- now opens the color-choice menu instead of resetting
// directly, same button slot/color.
void drawMenuButton() {
  drawTextButton(NEW_GAME_BTN_X, BTN_Y, BTN_W, BTN_H, "MENU", 4, TFT_DARKGREEN, TFT_GREEN);
}

// Amber rather than Menu's green, so the two are easy to tell apart at
// a glance -- a common "undo" color and distinct from "start over".
void drawUndoButton() {
  drawTextButton(UNDO_BTN_X, BTN_Y, BTN_W, BTN_H, "UNDO", 4, 0x8400 /* dark amber */, TFT_ORANGE);
}

bool isTouchInButton(int x, int y, int w, int h) {
  if (!touch.touched()) return false;
  TS_Point p = touch.getPoint();
  int tx = map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, 240);
  int ty = map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, 320);
  return (tx >= x && tx < x + w && ty >= y && ty < y + h);
}

bool isTouchOnMenuButton() {
  return isTouchInButton(NEW_GAME_BTN_X, BTN_Y, BTN_W, BTN_H);
}

bool isTouchOnUndoButton() {
  return isTouchInButton(UNDO_BTN_X, BTN_Y, BTN_W, BTN_H);
}

// ─── Menu screen ────────────────────────────────────────────────────────────
// Covers the whole screen (board/buttons hidden) while inMenu is true. Two
// independent things live here: starting a fresh game (NEW GAME, which goes
// on to the difficulty screen below), and choosing which color the human
// plays right now (PLAY WHITE/PLAY BLACK) -- the latter works standalone,
// mid-game, without resetting anything (see switchHumanColor()), so a real
// board position can have its human/AI sides swapped on demand -- a teaching
// technique ("finish what the other person started").
#define MENU_BTN_W          200
#define MENU_BTN_H          55
#define MENU_BTN_X          ((240 - MENU_BTN_W) / 2)
#define MENU_NEWGAME_BTN_Y  95
#define MENU_WHITE_BTN_Y    160
#define MENU_BLACK_BTN_Y    225

void drawMenuScreen() {
  tft.fillScreen(COLOR_BG);
  tft.setTextSize(2);
  tft.setTextColor(TFT_YELLOW, COLOR_BG);
  tft.setCursor(60, 28);
  tft.print("Menu");

  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, COLOR_BG);
  char caption[40];
  snprintf(caption, sizeof(caption), "Now playing: %s, %s",
           humanColor == WHITE_PIECE ? "White" : "Black", STRENGTH_NAMES[aiStrength]);
  tft.setCursor((240 - (int)strlen(caption) * 6) / 2, 58);
  tft.print(caption);

  drawTextButton(MENU_BTN_X, MENU_NEWGAME_BTN_Y, MENU_BTN_W, MENU_BTN_H,
                 "NEW GAME", 8, TFT_DARKGREEN, TFT_GREEN);
  drawTextButton(MENU_BTN_X, MENU_WHITE_BTN_Y, MENU_BTN_W, MENU_BTN_H,
                 "PLAY WHITE", 10, COLOR_WHITE_P, TFT_BLACK, TFT_BLACK);
  drawTextButton(MENU_BTN_X, MENU_BLACK_BTN_Y, MENU_BTN_W, MENU_BTN_H,
                 "PLAY BLACK", 10, COLOR_BLACK_P, TFT_WHITE, TFT_WHITE);
}

bool isTouchOnMenuNewGameButton() {
  return isTouchInButton(MENU_BTN_X, MENU_NEWGAME_BTN_Y, MENU_BTN_W, MENU_BTN_H);
}

bool isTouchOnPlayWhiteButton() {
  return isTouchInButton(MENU_BTN_X, MENU_WHITE_BTN_Y, MENU_BTN_W, MENU_BTN_H);
}

bool isTouchOnPlayBlackButton() {
  return isTouchInButton(MENU_BTN_X, MENU_BLACK_BTN_Y, MENU_BTN_W, MENU_BTN_H);
}

// ─── Difficulty screen (reached via MENU's NEW GAME button) ────────────────
// Picking a level here starts a fresh game via resetGame(), which uses
// whatever humanColor is currently set -- color is chosen separately, via
// MENU's own PLAY WHITE/PLAY BLACK buttons, and isn't part of this flow.
#define DIFF_BTN_W    200
#define DIFF_BTN_H    45
#define DIFF_BTN_X    ((240 - DIFF_BTN_W) / 2)
#define DIFF_BTN_GAP  10
#define DIFF_BTN_Y0   90
#define DIFF_BTN_Y(i) (DIFF_BTN_Y0 + (i) * (DIFF_BTN_H + DIFF_BTN_GAP))

void drawDifficultyMenu() {
  tft.fillScreen(COLOR_BG);
  tft.setTextSize(2);
  tft.setTextColor(TFT_YELLOW, COLOR_BG);
  tft.setCursor(28, 50);
  tft.print("Difficulty");

  // Light-to-dark progression (green..red), text color kept readable on each.
  uint16_t fill[4]    = {TFT_GREEN, TFT_YELLOW, TFT_ORANGE, TFT_RED};
  uint16_t border[4]  = {TFT_BLACK, TFT_BLACK,  TFT_BLACK,  TFT_WHITE};
  uint16_t textCol[4] = {TFT_BLACK, TFT_BLACK,  TFT_BLACK,  TFT_WHITE};
  int labelLen[4]     = {4, 6, 4, 6}; // strlen(STRENGTH_NAMES[i])

  for (int i = 0; i < 4; i++) {
    drawTextButton(DIFF_BTN_X, DIFF_BTN_Y(i), DIFF_BTN_W, DIFF_BTN_H,
                   STRENGTH_NAMES[i], labelLen[i], fill[i], border[i], textCol[i]);
  }
}

bool isTouchOnDifficultyButton(int idx) {
  return isTouchInButton(DIFF_BTN_X, DIFF_BTN_Y(idx), DIFF_BTN_W, DIFF_BTN_H);
}

// Changes which color the human plays *without* resetting the game -- supports
// switching sides mid-game (a teaching technique: "finish what the other
// person started"). If it's currently the new AI color's turn, the AI just
// picks up from here on the very next loop() iteration -- no special-casing
// needed, the same mechanism that already makes "AI moves first" work when
// starting a new game as Black.
void switchHumanColor(int newColor) {
  humanColor = newColor;
  pieceSelected = false;
  selectedRow = -1;
  selectedCol = -1;
  legalMoveCount = 0;
  tft.fillScreen(COLOR_BG);
  drawBoard(gs); // re-renders flipped/unflipped per the new boardFlipped()
  drawMenuButton();
  drawUndoButton();
  if (gameOver) drawGameOver(lastGameOverMsg);
  showTurnStatus();
}

void resetGame() {
  initBoard(gs);
  microMaxInit();
  mmNodeBudget = STRENGTH_NODE_BUDGET[aiStrength];
  bookReset();
  invalidateUndoSnapshot();
  pieceSelected = false;
  selectedRow = -1;
  selectedCol = -1;
  legalMoveCount = 0;
  gameOver = false;
  tft.fillScreen(COLOR_BG);
  drawBoard(gs);
  drawMenuButton();
  drawUndoButton();
  showTurnStatus();
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
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, COLOR_BG);
  for (int i = 0; i < 8; i++) {
    int boardCol = boardFlipped() ? 7 - i : i;
    tft.setCursor(BOARD_OFFSET_X + i * SQUARE_SIZE + 12, BOARD_OFFSET_Y + 8 * SQUARE_SIZE + 2);
    tft.print((char)('a' + boardCol));
  }
  for (int i = 0; i < 8; i++) {
    int boardRow = boardFlipped() ? 7 - i : i;
    tft.setCursor(BOARD_OFFSET_X + 8 * SQUARE_SIZE + 2, BOARD_OFFSET_Y + i * SQUARE_SIZE + 10);
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

// ─── Undo (single-level) ───────────────────────────────────────────────────
// One saved snapshot, taken right before the human's move is applied --
// restoring it always reverts to "right before my last move", which also
// erases whatever the AI replied with in between (there's no sensible way
// to undo only the AI's reply and leave the human's own move in place, and
// a kid using this to take back a blunder wants the whole exchange gone
// anyway). No stack, no redo -- exactly one level, by design, per Lars.
//
// Three separate pieces of state have to travel together or the AI and this
// sketch's own board desync: this sketch's own GameState, micro-Max's own
// board/running state (it never sees an undo otherwise -- it would still
// think the undone moves happened), and the opening book's move history
// (same reason -- otherwise it could offer a book move that no longer
// matches what's actually on the board).
struct UndoSnapshot {
  bool valid;
  GameState gs;
  signed char mmB[129];
  int mmJ, mmZ, mmk, mmR, mmQ, mmO;
  int plyCount;
  bool outOfBook;
  BookMove moveHistory[BOOK_MAX_PLY];
};
UndoSnapshot undoSnapshot = { false };

void invalidateUndoSnapshot() {
  undoSnapshot.valid = false;
}

void saveUndoSnapshot(GameState &g) {
  undoSnapshot.valid = true;
  undoSnapshot.gs = g;
  memcpy(undoSnapshot.mmB, mmB, sizeof(mmB));
  undoSnapshot.mmJ = mmJ; undoSnapshot.mmZ = mmZ; undoSnapshot.mmk = mmk;
  undoSnapshot.mmR = mmR; undoSnapshot.mmQ = mmQ; undoSnapshot.mmO = mmO;
  undoSnapshot.plyCount = plyCount;
  undoSnapshot.outOfBook = outOfBook;
  memcpy(undoSnapshot.moveHistory, moveHistory, sizeof(moveHistory));
}

// Returns false (and leaves everything untouched) if nothing's been saved
// yet this game -- caller just shows "Nothing to undo" rather than acting.
bool restoreUndoSnapshot(GameState &g) {
  if (!undoSnapshot.valid) return false;
  g = undoSnapshot.gs;
  memcpy(mmB, undoSnapshot.mmB, sizeof(mmB));
  mmJ = undoSnapshot.mmJ; mmZ = undoSnapshot.mmZ; mmk = undoSnapshot.mmk;
  mmR = undoSnapshot.mmR; mmQ = undoSnapshot.mmQ; mmO = undoSnapshot.mmO;
  plyCount = undoSnapshot.plyCount;
  outOfBook = undoSnapshot.outOfBook;
  memcpy(moveHistory, undoSnapshot.moveHistory, sizeof(moveHistory));
  undoSnapshot.valid = false; // one level only -- used up until the next move
  return true;
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

// ─── Setup & Loop ──────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // Backlight
  ledcAttach(TFT_BL_PIN, 5000, 8);
  ledcWrite(TFT_BL_PIN, 200);

  // TFT
  tft.init();
  tft.setRotation(0); // Portrait
  tft.fillScreen(COLOR_BG);

  // Touch SPI
  touchSPI.begin(TOUCH_SCK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS_PIN);
  touch.begin(touchSPI);
  touch.setRotation(0);

  // Init game
  initBoard(gs);
  microMaxInit();
  mmNodeBudget = STRENGTH_NODE_BUDGET[aiStrength];
  bookReset();
  invalidateUndoSnapshot();

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
  // PLAY WHITE/BLACK apply immediately (switchHumanColor(), no board reset,
  // works mid-game); NEW GAME moves on to the difficulty screen below.
  if (inMenu) {
    if (isTouchOnMenuNewGameButton()) {
      delay(50); // debounce
      if (isTouchOnMenuNewGameButton()) {
        unsigned long _waitStart = millis();
        while (touch.touched() && millis() - _waitStart < 2000) { delay(10); }
        inMenu = false;
        inDifficultyMenu = true;
        drawDifficultyMenu();
      }
      return;
    }
    if (isTouchOnPlayWhiteButton()) {
      delay(50); // debounce
      if (isTouchOnPlayWhiteButton()) {
        unsigned long _waitStart = millis();
        while (touch.touched() && millis() - _waitStart < 2000) { delay(10); }
        inMenu = false;
        switchHumanColor(WHITE_PIECE);
      }
      return;
    }
    if (isTouchOnPlayBlackButton()) {
      delay(50); // debounce
      if (isTouchOnPlayBlackButton()) {
        unsigned long _waitStart = millis();
        while (touch.touched() && millis() - _waitStart < 2000) { delay(10); }
        inMenu = false;
        switchHumanColor(BLACK_PIECE);
      }
      return;
    }
    return;
  }

  // Difficulty menu: MENU's NEW GAME button leads here. Picking a level
  // starts a fresh game at that difficulty, keeping whatever color is
  // currently set (color is a separate, standalone choice -- see above).
  if (inDifficultyMenu) {
    for (int i = 0; i < 4; i++) {
      if (isTouchOnDifficultyButton(i)) {
        delay(50); // debounce
        if (isTouchOnDifficultyButton(i)) {
          unsigned long _waitStart = millis();
          while (touch.touched() && millis() - _waitStart < 2000) { delay(10); }
          aiStrength = i;
          inDifficultyMenu = false;
          resetGame();
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
      drawMenuScreen();
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
      for (int i = 0; i < legalMoveCount; i++) {
        if (legalMoves[i].toRow == tRow && legalMoves[i].toCol == tCol) {
          saveUndoSnapshot(gs); // captures the position as it stood right before this move
          applyMove(gs, legalMoves[i]);
          microMaxApplyMove(legalMoves[i].fromRow, legalMoves[i].fromCol,
                            legalMoves[i].toRow, legalMoves[i].toCol);
          bookRecordMove(legalMoves[i].fromRow, legalMoves[i].fromCol,
                          legalMoves[i].toRow, legalMoves[i].toCol);
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

      // Check game end conditions after player move
      drawBoard(gs);

      if (isCheckmate(gs, -humanColor)) {
        drawStatus("Checkmate! You win!");
        drawGameOver("You Win!");
        gameOver = true;
        return;
      }
      if (isStalemate(gs, -humanColor)) {
        drawStatus("Stalemate!");
        drawGameOver("Stalemate!");
        gameOver = true;
        return;
      }
      if (isInCheck(gs, -humanColor)) {
        drawStatus("Check! AI thinking...");
      } else {
        drawStatus("AI thinking...");
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
        microMaxGetBestMove(mmFromRow, mmFromCol, mmToRow, mmToCol);
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

    drawBoard(gs);

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
      drawStatus("Checkmate! AI wins!");
      drawGameOver("AI Wins!");
      gameOver = true;
      return;
    }
    if (isStalemate(gs, humanColor)) {
      drawStatus("Stalemate!");
      drawGameOver("Stalemate!");
      gameOver = true;
      return;
    }
    if (isInCheck(gs, humanColor)) {
      drawStatus("You're in Check!");
    } else {
      showTurnStatus();
    }
  }
}
