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

// Set when the human's tapped move reaches the back rank -- the move
// itself is held here, promoteTo unset, until the promotion-choice screen
// picks a piece and completeHumanMove() actually applies it.
bool awaitingPromotion = false;
Move pendingPromotionMove;

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
  saveSettings(humanColor, aiStrength);
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
  saveSettings(humanColor, aiStrength); // see switchHumanColor()'s comment
}

void resetGame() {
  initBoard(gs);
  microMaxInit();
  mmNodeBudget = STRENGTH_NODE_BUDGET[aiStrength];
  bookReset();
  resetPositionHistory();
  recordPosition(gs); // the starting position itself counts as its own first occurrence
  invalidateUndoSnapshot();
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
  saveUndoSnapshot(gs); // captures the position as it stood right before this move
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
  loadSettings(humanColor, aiStrength);

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
  // PLAY WHITE/BLACK and DIFFICULTY all apply immediately (no board reset,
  // work mid-game); NEW GAME starts a fresh game using whatever color/
  // difficulty are currently set, rather than asking again.
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
      drawMenuScreen(humanColor, aiStrength);
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
    recordPosition(gs);
    savePersistedGame(gs);

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
