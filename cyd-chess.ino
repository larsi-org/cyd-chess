// Chess game for ESP32-2432S028R with ILI9341 TFT display
// Player (White) vs AI (Black) -- H.G. Muller's micro-Max engine plus a
// small opening book (see the "micro-Max chess engine" / "Opening Book"
// sections below)
// Touch screen for piece selection and movement, one-level undo

#include <SPI.h>

// Hoisted type definitions. Arduino's auto-prototype generator inserts
// function prototypes right before the first function definition in the
// file (getArduinoLoopTaskStackSize, below) — these structs must be fully
// defined before that point or the generated prototypes referencing
// Move*/GameState& fail to compile.
struct Move {
  int fromRow, fromCol, toRow, toCol;
  int capturedPiece, capturedColor;
  bool promotion;
  int promotedFrom;
  bool enPassant;
  bool castling;
  // castling rook info
  int rookFromCol, rookToCol;
};

struct GameState {
  int board[8][8];        // piece type
  int color[8][8];        // piece color
  bool whiteCastleK;      // white can castle kingside
  bool whiteCastleQ;      // white can castle queenside
  bool blackCastleK;
  bool blackCastleQ;
  int enPassantCol;       // column of en passant target (-1 if none)
  int enPassantRow;       // row of en passant target
  int currentPlayer;      // WHITE_PIECE or BLACK_PIECE
};

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
#include "pieces.h"
#include "book.h"

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

// Forward declarations
void initBoard(GameState &gs);
void drawStatus(const char *msg);
void drawGameOver(const char *msg);
void drawPiece(int row, int col, int piece, int pieceColor);
void drawSquare(int row, int col, bool highlight, bool moveDot, GameState &gs);
void drawBoard(GameState &gs);
void redrawSquare(GameState &gs, int r, int c);
void redrawSelectionChange(GameState &gs, int oldSelRow, int oldSelCol, bool oldDotSquare[8][8]);
bool inBounds(int r, int c);
bool squareAttacked(GameState &gs, int row, int col, int byPlayer);
bool isInCheck(GameState &gs, int player);
void addMove(Move *moves, int &count, int fr, int fc, int tr, int tc, GameState &gs, bool enPassant = false, bool castling = false, int rookFromCol = -1, int rookToCol = -1);
void generatePseudoMoves(GameState &gs, int player, Move *moves, int &count);
void applyMove(GameState &gs, Move &m);
void undoMove(GameState &gs, Move &m);
void generateMoves(GameState &gs, int player, Move *moves, int &count);
bool isCheckmate(GameState &gs, int player);
bool isStalemate(GameState &gs, int player);
void microMaxInit();
void microMaxApplyMove(int fromRow, int fromCol, int toRow, int toCol);
void microMaxGetBestMove(int &fromRow, int &fromCol, int &toRow, int &toCol);
void invalidateUndoSnapshot();
void drawUndoButton();
void getTouchSquare(int &row, int &col);

TFT_eSPI tft = TFT_eSPI();
SPIClass touchSPI(VSPI);
XPT2046_Touchscreen touch(TOUCH_CS_PIN, TOUCH_IRQ_PIN);

// ─── Chess Constants ───────────────────────────────────────────────────────
#define BOARD_SIZE    8
#define SQUARE_SIZE   30
#define BOARD_OFFSET_X 0
#define BOARD_OFFSET_Y 40

// Piece types
#define EMPTY  0
#define PAWN   1
#define KNIGHT 2
#define BISHOP 3
#define ROOK   4
#define QUEEN  5
#define KING   6

// Colors
#define WHITE_PIECE  1
#define BLACK_PIECE -1

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

// ─── Game State Structs ────────────────────────────────────────────────────




// ─── Forward Declarations ──────────────────────────────────────────────────
void initBoard(GameState &gs);
void drawBoard(GameState &gs);
void redrawSquare(GameState &gs, int r, int c);
void redrawSelectionChange(GameState &gs, int oldSelRow, int oldSelCol, bool oldDotSquare[8][8]);
void drawSquare(int row, int col, bool highlight, bool moveDot, GameState &gs);
void drawPiece(int row, int col, int piece, int pieceColor);
bool isValidMove(GameState &gs, Move &m);
void generateMoves(GameState &gs, int player, Move *moves, int &count);
void applyMove(GameState &gs, Move &m);
void undoMove(GameState &gs, Move &m);
bool isInCheck(GameState &gs, int player);
bool isCheckmate(GameState &gs, int player);
bool isStalemate(GameState &gs, int player);
void microMaxInit();
void microMaxApplyMove(int fromRow, int fromCol, int toRow, int toCol);
void microMaxGetBestMove(int &fromRow, int &fromCol, int &toRow, int &toCol);
void invalidateUndoSnapshot();
void drawUndoButton();
void getTouchSquare(int &row, int &col);
void drawStatus(const char *msg);
void drawGameOver(const char *msg);
bool squareAttacked(GameState &gs, int row, int col, int byPlayer);
void handlePromotion(GameState &gs, int row, int col, int pieceColor);

// ─── Globals ───────────────────────────────────────────────────────────────
GameState gs;
int selectedRow = -1;
int selectedCol = -1;
bool pieceSelected = false;
Move legalMoves[256];
int legalMoveCount = 0;
bool gameOver = false;
char statusMsg[64] = "Your turn (White)";

// Touch calibration values (may need tuning)
#define TOUCH_X_MIN 200
#define TOUCH_X_MAX 3800
#define TOUCH_Y_MIN 300
#define TOUCH_Y_MAX 3700

// ─── Board Initialization ──────────────────────────────────────────────────
void initBoard(GameState &gs) {
  memset(gs.board, 0, sizeof(gs.board));
  memset(gs.color, 0, sizeof(gs.color));

  // Back rows
  int backRow[8] = {ROOK, KNIGHT, BISHOP, QUEEN, KING, BISHOP, KNIGHT, ROOK};

  for (int c = 0; c < 8; c++) {
    // Black back row (row 0)
    gs.board[0][c] = backRow[c];
    gs.color[0][c] = BLACK_PIECE;
    // Black pawns (row 1)
    gs.board[1][c] = PAWN;
    gs.color[1][c] = BLACK_PIECE;
    // White pawns (row 6)
    gs.board[6][c] = PAWN;
    gs.color[6][c] = WHITE_PIECE;
    // White back row (row 7)
    gs.board[7][c] = backRow[c];
    gs.color[7][c] = WHITE_PIECE;
  }

  gs.whiteCastleK = true;
  gs.whiteCastleQ = true;
  gs.blackCastleK = true;
  gs.blackCastleQ = true;
  gs.enPassantCol = -1;
  gs.enPassantRow = -1;
  gs.currentPlayer = WHITE_PIECE;
}

// ─── Drawing Functions ─────────────────────────────────────────────────────
void drawStatus(const char *msg) {
  tft.fillRect(0, 0, 240, 38, COLOR_STATUS_BG);
  tft.setTextColor(COLOR_TEXT, COLOR_STATUS_BG);
  tft.setTextSize(1);
  tft.setCursor(4, 4);
  tft.print(msg);
}

void drawGameOver(const char *msg) {
  tft.fillRect(20, 100, 200, 60, TFT_RED);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.setTextSize(2);
  tft.setCursor(30, 115);
  tft.print(msg);
  tft.setTextSize(1);
  tft.setCursor(50, 140);
  tft.print("Tap to restart");
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

void drawPiece(int row, int col, int piece, int pieceColor) {
  int x = BOARD_OFFSET_X + col * SQUARE_SIZE + (SQUARE_SIZE - PIECE_SIZE) / 2;
  int y = BOARD_OFFSET_Y + row * SQUARE_SIZE + (SQUARE_SIZE - PIECE_SIZE) / 2;

  int idx = piece - 1; // PAWN=1..KING=6 -> 0..5
  if (idx < 0 || idx > 5) return;

  uint16_t fg = (pieceColor == WHITE_PIECE) ? COLOR_WHITE_P : COLOR_BLACK_P;
  uint16_t outline = (pieceColor == WHITE_PIECE) ? TFT_BLACK : TFT_WHITE;

  drawPieceBitmap(x, y, PIECE_FILL[idx], fg);
  drawPieceBitmap(x, y, PIECE_OUTLINE[idx], outline);
}

void drawSquare(int row, int col, bool highlight, bool moveDot, GameState &gs) {
  int x = BOARD_OFFSET_X + col * SQUARE_SIZE;
  int y = BOARD_OFFSET_Y + row * SQUARE_SIZE;
  uint16_t bg;

  if (highlight) {
    bg = COLOR_SELECTED;
  } else {
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
                     uint16_t fillColor, uint16_t borderColor) {
  tft.fillRoundRect(x, y, w, h, 4, fillColor);
  tft.drawRoundRect(x, y, w, h, 4, borderColor);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, fillColor);
  // Centred-ish text — TFT_eSPI default font is ~6 pixels per character.
  tft.setCursor(x + (w - labelLen * 6) / 2, y + (h - 8) / 2);
  tft.print(label);
}

void drawNewGameButton() {
  drawTextButton(NEW_GAME_BTN_X, BTN_Y, BTN_W, BTN_H, "NEW GAME", 8, TFT_DARKGREEN, TFT_GREEN);
}

// Amber rather than New Game's green, so the two are easy to tell apart at
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

bool isTouchOnNewGameButton() {
  return isTouchInButton(NEW_GAME_BTN_X, BTN_Y, BTN_W, BTN_H);
}

bool isTouchOnUndoButton() {
  return isTouchInButton(UNDO_BTN_X, BTN_Y, BTN_W, BTN_H);
}

void resetGame() {
  initBoard(gs);
  microMaxInit();
  bookReset();
  invalidateUndoSnapshot();
  pieceSelected = false;
  selectedRow = -1;
  selectedCol = -1;
  legalMoveCount = 0;
  gameOver = false;
  tft.fillScreen(COLOR_BG);
  drawBoard(gs);
  drawNewGameButton();
  drawUndoButton();
  drawStatus("Your turn (White)");
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

  // Draw coordinates
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, COLOR_BG);
  for (int c = 0; c < 8; c++) {
    tft.setCursor(BOARD_OFFSET_X + c * SQUARE_SIZE + 12, BOARD_OFFSET_Y + 8 * SQUARE_SIZE + 2);
    tft.print((char)('a' + c));
  }
  for (int r = 0; r < 8; r++) {
    tft.setCursor(BOARD_OFFSET_X + 8 * SQUARE_SIZE + 2, BOARD_OFFSET_Y + r * SQUARE_SIZE + 10);
    tft.print(8 - r);
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

// ─── Move Validation Helpers ───────────────────────────────────────────────
bool inBounds(int r, int c) {
  return r >= 0 && r < 8 && c >= 0 && c < 8;
}

bool squareAttacked(GameState &gs, int row, int col, int byPlayer) {
  // Check if (row,col) is attacked by 'byPlayer'
  // Pawn attacks
  int pawnDir = (byPlayer == WHITE_PIECE) ? 1 : -1;
  int pawnRow = row + pawnDir;
  if (inBounds(pawnRow, col - 1) && gs.board[pawnRow][col - 1] == PAWN && gs.color[pawnRow][col - 1] == byPlayer) return true;
  if (inBounds(pawnRow, col + 1) && gs.board[pawnRow][col + 1] == PAWN && gs.color[pawnRow][col + 1] == byPlayer) return true;

  // Knight attacks
  int kd[8][2] = {{-2,-1},{-2,1},{-1,-2},{-1,2},{1,-2},{1,2},{2,-1},{2,1}};
  for (int i = 0; i < 8; i++) {
    int nr = row + kd[i][0], nc = col + kd[i][1];
    if (inBounds(nr, nc) && gs.board[nr][nc] == KNIGHT && gs.color[nr][nc] == byPlayer) return true;
  }

  // Sliding pieces (Bishop/Queen diagonals)
  int diagD[4][2] = {{1,1},{1,-1},{-1,1},{-1,-1}};
  for (int d = 0; d < 4; d++) {
    int nr = row + diagD[d][0], nc = col + diagD[d][1];
    while (inBounds(nr, nc)) {
      if (gs.board[nr][nc] != EMPTY) {
        if (gs.color[nr][nc] == byPlayer && (gs.board[nr][nc] == BISHOP || gs.board[nr][nc] == QUEEN)) return true;
        break;
      }
      nr += diagD[d][0]; nc += diagD[d][1];
    }
  }

  // Sliding pieces (Rook/Queen straight)
  int straightD[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
  for (int d = 0; d < 4; d++) {
    int nr = row + straightD[d][0], nc = col + straightD[d][1];
    while (inBounds(nr, nc)) {
      if (gs.board[nr][nc] != EMPTY) {
        if (gs.color[nr][nc] == byPlayer && (gs.board[nr][nc] == ROOK || gs.board[nr][nc] == QUEEN)) return true;
        break;
      }
      nr += straightD[d][0]; nc += straightD[d][1];
    }
  }

  // King
  for (int dr = -1; dr <= 1; dr++) {
    for (int dc = -1; dc <= 1; dc++) {
      if (dr == 0 && dc == 0) continue;
      int nr = row + dr, nc = col + dc;
      if (inBounds(nr, nc) && gs.board[nr][nc] == KING && gs.color[nr][nc] == byPlayer) return true;
    }
  }

  return false;
}

bool isInCheck(GameState &gs, int player) {
  int kingRow = -1, kingCol = -1;
  for (int r = 0; r < 8 && kingRow == -1; r++) {
    for (int c = 0; c < 8 && kingRow == -1; c++) {
      if (gs.board[r][c] == KING && gs.color[r][c] == player) {
        kingRow = r; kingCol = c;
      }
    }
  }
  if (kingRow == -1) return false;
  int opponent = -player;
  return squareAttacked(gs, kingRow, kingCol, opponent);
}

// ─── Move Generation ───────────────────────────────────────────────────────
void addMove(Move *moves,  int &count,  int fr,  int fc,  int tr,  int tc,  GameState &gs,
             bool enPassant,  bool castling,  int rookFromCol,  int rookToCol) {
  if (count >= 255) return;
  Move m;
  m.fromRow = fr; m.fromCol = fc;
  m.toRow = tr; m.toCol = tc;
  m.capturedPiece = gs.board[tr][tc];
  m.capturedColor = gs.color[tr][tc];
  m.promotion = false;
  m.promotedFrom = gs.board[fr][fc];
  m.enPassant = enPassant;
  m.castling = castling;
  m.rookFromCol = rookFromCol;
  m.rookToCol = rookToCol;

  // Pawn promotion
  if (gs.board[fr][fc] == PAWN) {
    if ((gs.color[fr][fc] == WHITE_PIECE && tr == 0) ||
        (gs.color[fr][fc] == BLACK_PIECE && tr == 7)) {
      m.promotion = true;
    }
  }
  moves[count++] = m;
}

void generatePseudoMoves(GameState &gs, int player, Move *moves, int &count) {
  int dir = (player == WHITE_PIECE) ? -1 : 1;

  for (int r = 0; r < 8; r++) {
    for (int c = 0; c < 8; c++) {
      if (gs.board[r][c] == EMPTY || gs.color[r][c] != player) continue;
      int piece = gs.board[r][c];

      if (piece == PAWN) {
        int nr = r + dir;
        // Forward
        if (inBounds(nr, c) && gs.board[nr][c] == EMPTY) {
          addMove(moves, count, r, c, nr, c, gs);
          // Double push from start
          int startRow = (player == WHITE_PIECE) ? 6 : 1;
          if (r == startRow && gs.board[nr + dir][c] == EMPTY) {
            addMove(moves, count, r, c, nr + dir, c, gs);
          }
        }
        // Captures
        for (int dc = -1; dc <= 1; dc += 2) {
          int nc = c + dc;
          if (!inBounds(nr, nc)) continue;
          if (gs.board[nr][nc] != EMPTY && gs.color[nr][nc] != player) {
            addMove(moves, count, r, c, nr, nc, gs);
          }
          // En passant
          if (gs.enPassantCol == nc && gs.enPassantRow == nr) {
            addMove(moves, count, r, c, nr, nc, gs, true);
          }
        }
      }

      else if (piece == KNIGHT) {
        int kd[8][2] = {{-2,-1},{-2,1},{-1,-2},{-1,2},{1,-2},{1,2},{2,-1},{2,1}};
        for (int i = 0; i < 8; i++) {
          int nr = r + kd[i][0], nc = c + kd[i][1];
          if (!inBounds(nr, nc)) continue;
          if (gs.board[nr][nc] == EMPTY || gs.color[nr][nc] != player) {
            addMove(moves, count, r, c, nr, nc, gs);
          }
        }
      }

      else if (piece == BISHOP || piece == QUEEN) {
        int diagD[4][2] = {{1,1},{1,-1},{-1,1},{-1,-1}};
        for (int d = 0; d < 4; d++) {
          int nr = r + diagD[d][0], nc = c + diagD[d][1];
          while (inBounds(nr, nc)) {
            if (gs.board[nr][nc] != EMPTY) {
              if (gs.color[nr][nc] != player) addMove(moves, count, r, c, nr, nc, gs);
              break;
            }
            addMove(moves, count, r, c, nr, nc, gs);
            nr += diagD[d][0]; nc += diagD[d][1];
          }
        }
      }

      if (piece == ROOK || piece == QUEEN) {
        int straightD[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
        for (int d = 0; d < 4; d++) {
          int nr = r + straightD[d][0], nc = c + straightD[d][1];
          while (inBounds(nr, nc)) {
            if (gs.board[nr][nc] != EMPTY) {
              if (gs.color[nr][nc] != player) addMove(moves, count, r, c, nr, nc, gs);
              break;
            }
            addMove(moves, count, r, c, nr, nc, gs);
            nr += straightD[d][0]; nc += straightD[d][1];
          }
        }
      }

      else if (piece == KING) {
        for (int dr = -1; dr <= 1; dr++) {
          for (int dc = -1; dc <= 1; dc++) {
            if (dr == 0 && dc == 0) continue;
            int nr = r + dr, nc = c + dc;
            if (!inBounds(nr, nc)) continue;
            if (gs.board[nr][nc] == EMPTY || gs.color[nr][nc] != player) {
              addMove(moves, count, r, c, nr, nc, gs);
            }
          }
        }
        // Castling
        int backRow = (player == WHITE_PIECE) ? 7 : 0;
        int opponent = -player;
        if (r == backRow && c == 4 && !isInCheck(gs, player)) {
          // Kingside
          bool canK = (player == WHITE_PIECE) ? gs.whiteCastleK : gs.blackCastleK;
          if (canK && gs.board[backRow][5] == EMPTY && gs.board[backRow][6] == EMPTY &&
              !squareAttacked(gs, backRow, 5, opponent) && !squareAttacked(gs, backRow, 6, opponent)) {
            addMove(moves, count, r, c, backRow, 6, gs, false, true, 7, 5);
          }
          // Queenside
          bool canQ = (player == WHITE_PIECE) ? gs.whiteCastleQ : gs.blackCastleQ;
          if (canQ && gs.board[backRow][3] == EMPTY && gs.board[backRow][2] == EMPTY && gs.board[backRow][1] == EMPTY &&
              !squareAttacked(gs, backRow, 3, opponent) && !squareAttacked(gs, backRow, 2, opponent)) {
            addMove(moves, count, r, c, backRow, 2, gs, false, true, 0, 3);
          }
        }
      }
    }
  }
}

void applyMove(GameState &gs, Move &m) {
  int piece = gs.board[m.fromRow][m.fromCol];
  int pieceColor = gs.color[m.fromRow][m.fromCol];

  // Handle en passant capture
  if (m.enPassant) {
    int captureRow = m.fromRow;
    gs.board[captureRow][m.toCol] = EMPTY;
    gs.color[captureRow][m.toCol] = 0;
  }

  // Set new en passant target
  gs.enPassantCol = -1;
  gs.enPassantRow = -1;
  if (piece == PAWN && abs(m.toRow - m.fromRow) == 2) {
    gs.enPassantRow = (m.fromRow + m.toRow) / 2;
    gs.enPassantCol = m.fromCol;
  }

  // Move piece
  gs.board[m.toRow][m.toCol] = piece;
  gs.color[m.toRow][m.toCol] = pieceColor;
  gs.board[m.fromRow][m.fromCol] = EMPTY;
  gs.color[m.fromRow][m.fromCol] = 0;

  // Promotion
  if (m.promotion) {
    gs.board[m.toRow][m.toCol] = QUEEN;
  }

  // Castling: move rook
  if (m.castling) {
    int backRow = m.fromRow;
    gs.board[backRow][m.rookToCol] = ROOK;
    gs.color[backRow][m.rookToCol] = pieceColor;
    gs.board[backRow][m.rookFromCol] = EMPTY;
    gs.color[backRow][m.rookFromCol] = 0;
  }

  // Update castling rights
  if (piece == KING) {
    if (pieceColor == WHITE_PIECE) { gs.whiteCastleK = false; gs.whiteCastleQ = false; }
    else { gs.blackCastleK = false; gs.blackCastleQ = false; }
  }
  if (piece == ROOK) {
    if (pieceColor == WHITE_PIECE) {
      if (m.fromCol == 7) gs.whiteCastleK = false;
      if (m.fromCol == 0) gs.whiteCastleQ = false;
    } else {
      if (m.fromCol == 7) gs.blackCastleK = false;
      if (m.fromCol == 0) gs.blackCastleQ = false;
    }
  }

  gs.currentPlayer = -gs.currentPlayer;
}

void undoMove(GameState &gs, Move &m) {
  int piece = gs.board[m.toRow][m.toCol];
  int pieceColor = gs.color[m.toRow][m.toCol];

  // Undo promotion
  if (m.promotion) {
    piece = PAWN;
  }

  gs.board[m.fromRow][m.fromCol] = piece;
  gs.color[m.fromRow][m.fromCol] = pieceColor;
  gs.board[m.toRow][m.toCol] = m.capturedPiece;
  gs.color[m.toRow][m.toCol] = m.capturedColor;

  // Undo en passant
  if (m.enPassant) {
    int captureRow = m.fromRow;
    int captureColor = -pieceColor;
    gs.board[captureRow][m.toCol] = PAWN;
    gs.color[captureRow][m.toCol] = captureColor;
  }

  // Undo castling: move rook back
  if (m.castling) {
    int backRow = m.fromRow;
    gs.board[backRow][m.rookFromCol] = ROOK;
    gs.color[backRow][m.rookFromCol] = pieceColor;
    gs.board[backRow][m.rookToCol] = EMPTY;
    gs.color[backRow][m.rookToCol] = 0;
  }

  gs.currentPlayer = -gs.currentPlayer;
}

void generateMoves(GameState &gs, int player, Move *moves, int &count) {
  count = 0;
  Move pseudoMoves[256];
  int pseudoCount = 0;
  generatePseudoMoves(gs, player, pseudoMoves, pseudoCount);

  // Snapshot/restore the full GameState rather than relying on undoMove —
  // undoMove only restores board[][] / color[][], leaving enPassant target,
  // castling rights, and currentPlayer corrupted. With those leaking across
  // pseudoMove iterations, the AI sees positions that can't actually arise
  // and emits illegal moves (e.g. rook capturing its own pawn). Full copy
  // costs ~520 bytes per iteration but eliminates the whole class of bugs.
  for (int i = 0; i < pseudoCount; i++) {
    GameState saved = gs;
    applyMove(gs, pseudoMoves[i]);
    if (!isInCheck(gs, player)) {
      moves[count++] = pseudoMoves[i];
    }
    gs = saved;
  }
}

bool isCheckmate(GameState &gs, int player) {
  Move moves[256];
  int count = 0;
  generateMoves(gs, player, moves, count);
  return (count == 0 && isInCheck(gs, player));
}

bool isStalemate(GameState &gs, int player) {
  Move moves[256];
  int count = 0;
  generateMoves(gs, player, moves, count);
  return (count == 0 && !isInCheck(gs, player));
}

// ─── micro-Max chess engine (Black's AI) ───────────────────────────────────
// H.G. Muller's micro-Max 4.8 (home.hccnet.nl/h.g.muller/umax4_8.c), vendored
// near-verbatim in place of this sketch's earlier plain minimax/PST AI.
// Deliberately kept as its own separate board/search state (mmB[], mmA[]
// hash table, etc.) rather than adapted to run against GameState directly --
// the two board representations (this sketch's 8x8 arrays vs. micro-Max's
// packed 0x88 char board) are too different to safely reconcile inside code
// this dense without real risk of a subtle bug. Instead both boards are kept
// in lockstep: every move -- the player's (already validated by this
// sketch's own generateMoves()) and micro-Max's own -- gets applied to
// *both* representations, and micro-Max's chosen move is cross-checked
// against this sketch's own legal-move list before being trusted (see
// microMaxGetBestMove below), rather than trusting its output blindly.
//
// The interface is deliberately narrow -- just square coordinates in and
// out, never piece types or move-kind flags -- because this sketch's own
// generateMoves() already derives promotion/en-passant/castling from
// from/to squares plus its own tracked game state, so micro-Max's board
// representation and piece encoding never need to be understood at all,
// only its square-numbering: K = file_char - 16*rank_char + 799 (its own
// input-parsing formula, from its reference console UI) reduces to plain
// 16*row+col once row 0 = rank 8 -- which is already this sketch's own
// row/col convention -- so coordinates need no translation beyond that.
//
// Changes from Muller's original source (everything else is byte-for-byte
// as published):
//  - D()'s and main()'s old K&R-style declarations rewritten in standard
//    C++ function-signature form (a plain C++ requirement, not a logic
//    change) -- the body is untouched.
//  - Hash table size (U) shrunk from 1<<24 (16M entries, ~200MB) to
//    MM_U below, to fit ESP32 RAM. A smaller table only means more hash
//    collisions (weaker move-ordering hints), never incorrect play.
//  - Root search node budget (the literal `N<1e6` in the deepening-loop
//    condition) replaced with MM_NODE_BUDGET, sized for this hardware's
//    measured node rate rather than Muller's original PC target -- see
//    that macro's own comment for the measurement and reasoning.
//  - char forced explicitly `signed char` throughout this block -- see
//    that declaration's own comment.
//  - mmT[] (the hash translation table, reinterpreted 4 bytes at a time via
//    the K(A,B) macro) gets an explicit 4-byte alignment attribute --
//    Muller's own computed byte-offsets always land on a 4-byte boundary
//    (by construction, not by luck), but nothing in the C++ standard
//    otherwise guarantees mmT[]'s base address itself is 4-byte aligned,
//    which Xtensa's strict-alignment load/store unit requires.
//  - Two lines added right where the original already privately decides
//    "this is the move to commit" (previously a commented-out printf for
//    kibitzing) to capture (x, y) into mmLastX/mmLastY, so the caller can
//    learn which move was just played -- nothing else exposes that.
//  - A periodic vTaskDelay(1) added at the top of every mmD() call (own
//    dedicated counter, not reusing Muller's mmN -- mmN only increments once
//    per already-full-board-scan iteration at a single node, far too coarse
//    a yield granularity for the interrupt watchdog here), so a long search
//    still feeds the watchdog (same reasoning as the old minimax's per-node
//    yield it replaces).
//  - main()'s interactive read/print loop is dropped entirely (folded into
//    microMaxInit()/microMaxApplyMove()/microMaxGetBestMove() below instead)
//    -- an Arduino sketch supplies its own main(), so keeping Muller's would
//    have been a straight link conflict, not just unwanted code.

#define W while // Muller's, #undef'd right after microMaxInit() below to limit its reach
#define MM_U (1 << 12) // hash table size, must stay a power of 2 (see U-1 mask below)

// [cyd-chess addition] Muller's original root search budget was a literal
// `N<1e6` (a million nodes), tuned for 2000s-era PC hardware. Measured on
// this board: ~16,300 nodes/sec, and the search overshoots this threshold
// by roughly 2.8x before the current "iteration in flight" actually stops
// (the budget only gates whether the root *starts* another deepening pass,
// not recursive descents already underway) -- so 1e6 nodes cost ~172
// *seconds* for a cold-hash-table move, unusable for an interactive game.
// 30000 targets roughly 5s per move (every move -- see the mmN reset in
// microMaxGetBestMove() below for why this budget actually applies fresh
// each move rather than only the first). Tune this directly if 5s feels
// too slow/fast once played for real.
#define MM_NODE_BUDGET 30000
struct _ { int K, V; char X, Y, D; } mmA[MM_U];

int mmM = 136, mmS = 128, mmI = 8e3, mmQ, mmO, mmK, mmN, mmR, mmJ, mmZ, mmk = 16;
// (Muller's *p, c[9] globals dropped -- only used by the console getchar()
// input loop this port omits entirely.)
// [cyd-chess: char forced explicitly signed throughout this block --
//  Muller's original targeted x86, where plain `char` defaults signed;
//  Xtensa/ESP32's default is unsigned, which would silently corrupt the
//  negative piece-values/step-vectors this engine's arithmetic depends on]
signed char mmL,
mmW[] = {0, 2, 2, 7, -1, 8, 12, 23},                      /* relative piece values    */
mmOv[] = {-16,-15,-17,0,1,16,0,1,16,15,17,0,14,18,31,33,0, /* step-vector lists */
     7,-1,11,6,8,3,6,                          /* 1st dir. in o[] per piece*/
     6,3,5,7,4,5,3,6},                         /* initial piece setup      */
mmB[129],                                        /* board: half of 16x8+dummy*/
mmT[1035] __attribute__((aligned(4)));           /* hash translation table   */

int mmLastX, mmLastY; // [cyd-chess addition] last committed move's squares

#define MM_K(A,B) *(int*)(mmT+A+(B&8)+mmS*(B&7))
#define MM_J(A) MM_K(y+A,mmB[y])-MM_K(x+A,u)-MM_K(H+A,t)

int mmD(int q, int l, int e, int E, int z, int n)  /* recursive minimax search, k=moving side, n=depth*/
{                       /* e=score, z=prev.dest; J,Z=hashkeys; return score*/
 int j,r,m,v,d,h,i,F,G,V,P,f=mmJ,g=mmZ,C,s;
 signed char t,p,u,x,y,X,Y,H,B;
 struct _*a=mmA+(mmJ+mmk*E&MM_U-1);                     /* lookup pos. in hash table*/

 // [cyd-chess addition] feed the watchdog on every recursive call, not just
 // once per iterative-deepening step at a node (mmN, below, only increments
 // once per *already-full-board-scan* iteration at a single node -- far too
 // coarse a granularity: one such scan can itself contain thousands of
 // deeper recursive calls, taking far longer between yields than the
 // interrupt watchdog tolerates. Confirmed by hardware testing: with only
 // the mmN-based yield, the very first search call triggered a
 // TG1WDT_SYS_RESET reboot loop before ever returning.
 static uint32_t s_mmYieldCounter = 0;
 if ((++s_mmYieldCounter & 63) == 0) vTaskDelay(1);

 q--;                                          /* adj. window: delay bonus */
 mmk^=24;                                        /* change sides             */
 d=a->D;m=a->V;X=a->X;Y=a->Y;                  /* resume at stored depth   */
 if(a->K-mmZ|z|                                  /* miss: other pos. or empty*/
  !(m<=q|X&8&&m>=l|X&mmS))                       /*   or window incompatible */
  d=Y=0;                                       /* start iter. from scratch */
 X&=~mmM;                                        /* start at best-move hint  */

 W(d++<n||d<3||                                /* iterative deepening loop */
   z&mmK==mmI&&(mmN<MM_NODE_BUDGET&d<98||             /* root: deepen upto time   */
   (mmK=X,mmL=Y&~mmM,d=3)))                              /* time's up: go do best    */
 {x=B=X;                                       /* start scan at prev. best */
  h=Y&mmS;                                       /* request try noncastl. 1st*/
  P=d<3?mmI:mmD(-l,1-l,-e,mmS,0,d-3);                /* Search null move         */
  m=-P<l|mmR>35?d>2?-mmI:e:-P;                     /* Prune or stand-pat       */
  mmN++;                                         /* node count (for timing) */
  do{u=mmB[x];                                   /*  scan board looking for   */
   if(u&mmk)                                     /*  own piece (inefficient!)*/
   {r=p=u&7;                                   /* p = piece type (set r>0) */
    j=mmOv[p+16];                              /* first step vector f.piece*/
    W(r=p>2&r<0?-r:-mmOv[++j])                    /* loop over directions o[] */
    {A:                                        /* resume normal after best */
     y=x;F=G=mmS;                                /* (x,y)=move, (F,G)=castl.R*/
     do{                                       /* y traverses ray, or:     */
      H=y=h?Y^h:y+r;                           /* sneak in prev. best move */
      if(y&mmM)break;                            /* board edge hit           */
      m=E-mmS&mmB[E]&&y-E<2&E-y<2?mmI:m;             /* bad castling             */
      if(p<3&y==E)H^=16;                       /* shift capt.sqr. H if e.p.*/
      t=mmB[H];if(t&mmk|p<3&!(y-x&7)-!t)break;     /* capt. own, bad pawn mode */
      i=37*mmW[t&7]+(t&192);                     /* value of capt. piece t   */
      m=i<0?mmI:m;                               /* K capture                */
      if(m>=l&d>1)goto C;                      /* abort on fail high       */

      v=d-1?e:i-p;                             /* MVV/LVA scoring          */
      if(d-!t>1)                               /* remaining depth          */
      {v=p<6?mmB[x+8]-mmB[y+8]:0;                  /* center positional pts.   */
       mmB[G]=mmB[H]=mmB[x]=0;mmB[y]=u|32;             /* do move, set non-virgin  */
       if(!(G&mmM))mmB[F]=mmk+6,v+=50;               /* castling: put R & score  */
       v-=p-4|mmR>29?0:20;                       /* penalize mid-game K move */
       if(p<3)                                 /* pawns:                   */
       {v-=9*((x-2&mmM||mmB[x-2]-u)+               /* structure, undefended    */
              (x+2&mmM||mmB[x+2]-u)-1              /*        squares plus bias */
             +(mmB[x^16]==mmk+36))                 /* kling to non-virgin King */
             -(mmR>>2);                          /* end-game Pawn-push bonus */
        V=y+r+1&mmS?647-p:2*(u&y+16&32);         /* promotion or 6/7th bonus */
        mmB[y]+=V;i+=V;                          /* change piece, add score  */
       }
       v+=e+i;V=m>q?m:q;                       /* new eval and alpha       */
       mmJ+=MM_J(0);mmZ+=MM_J(8)+G-mmS;                /* update hash key          */
       C=d-1-(d>5&p>2&!t&!h);
       C=mmR>29|d<3|P-mmI?C:d;                     /* extend 1 ply if in check */
       do
        s=C>2|v>V?-mmD(-l,-V,-v,                 /* recursive eval. of reply */
                              F,0,C):v;        /* or fail low if futile    */
       W(s>q&++C<d);v=s;
       if(z&&mmK-mmI&&v+mmI&&x==mmK&y==mmL)              /* move pending & in root:  */
       {mmQ=-e-i;mmO=F;                            /*   exit if legal & found  */
        a->D=99;a->V=0;                        /* lock game in hash as draw*/
        mmLastX=x;mmLastY=y;                    /* [cyd-chess addition] capture committed move */
        mmR+=i>>7;return l;                      /* captured non-P material  */
       }
       mmJ=f;mmZ=g;                                /* restore hash key         */
       mmB[G]=mmk+6;mmB[F]=mmB[y]=0;mmB[x]=u;mmB[H]=t;     /* undo move,G can be dummy */
      }
      if(v>m)                                  /* new best, update max,best*/
       m=v,X=x,Y=y|mmS&F;                        /* mark double move with S  */
      if(h){h=0;goto A;}                       /* redo after doing old best*/
      if(x+r-y|u&32|                           /* not 1st step,moved before*/
         p>2&(p-4|j-7||                        /* no P & no lateral K move,*/
         mmB[G=x+3^r>>1&7]-mmk-6                   /* no virgin R in corner G, */
         ||mmB[G^1]|mmB[G^2])                      /* no 2 empty sq. next to R */
        )t+=p<5;                               /* fake capt. for nonsliding*/
      else F=y;                                /* enable e.p.              */
     }W(!t);                                   /* if not capt. continue ray*/
  }}}W((x=x+9&~mmM)-B);                          /* next sqr. of board, wrap */
C:if(m>mmI-mmM|m<mmM-mmI)d=98;                       /* mate holds to any depth  */
  m=m+mmI|P==mmI?m:0;                          /* best loses K: (stale)mate*/
  if(a->D<99)                                  /* protect game history     */
   a->K=mmZ,a->V=m,a->D=d,                       /* always store in hash tab */
   a->X=X|8*(m>q)|mmS*(m<l),a->Y=Y;              /* move, type (bound/exact),*/
 }                                             /*    encoded in X S,8 bits */
 mmk^=24;                                        /* change sides back        */
 return m+=m<e;                                /* delayed-loss bonus       */
}

void microMaxInit() {
 memset(mmA, 0, sizeof(mmA));      // [cyd-chess addition] clear hash between games
 mmK=8;W(mmK--)
 {mmB[mmK]=(mmB[mmK+112]=mmOv[mmK+24]+8)+8;mmB[mmK+16]=18;mmB[mmK+96]=9;  /* initial board setup*/
  mmL=8;W(mmL--)mmB[16*mmL+mmK+8]=(mmK-4)*(mmK-4)+(mmL-3.5)*(mmL-3.5); /* center-pts table   */
 }                                                   /*(in unused half b[])*/
 mmN=1035;W(mmN-->mmM)mmT[mmN]=rand()>>9;
 mmJ=mmZ=mmQ=mmO=mmN=mmR=0;        // [cyd-chess addition] reset per-game running state
 mmk=16;                           // [cyd-chess addition] White moves first
}
#undef W // limit this generic-named macro to just the vendored block above

void microMaxApplyMove(int fromRow, int fromCol, int toRow, int toCol) {
 mmK = fromRow * 16 + fromCol;
 mmL = toRow * 16 + toCol;
 mmD(-mmI, mmI, mmQ, mmO, 1, 3);
}

// Runs micro-Max's own search (no pending move given, so mmK stays at the
// sentinel mmI until the search itself sets it -- see the long comment
// above), then reports the from/to squares of whatever it just committed
// to its own board. Caller is expected to cross-check this against its own
// legal-move list before trusting it (this sketch's loop() does).
void microMaxGetBestMove(int &fromRow, int &fromCol, int &toRow, int &toCol) {
 // [cyd-chess fix] mmN (the root deepening loop's node counter, checked
 // against MM_NODE_BUDGET) is never reset by Muller's own code -- it only
 // resets once per NEW GAME, in microMaxInit(). Left alone, the first move
 // of a game exhausts the whole budget and every later move's root call
 // starts already over budget, so its "keep deepening" check fails
 // immediately and it silently falls back to the bare depth-3 search with
 // no time-based extension at all -- for the rest of that game. (This is
 // what actually made moves 2+ fast in the ~4s/cold-move measurement
 // documented above; earlier attribution of that to hash-table warming was
 // wrong.) Reset here so every move gets the real search budget.
 mmN = 0;
 mmK = mmI;
 mmD(-mmI, mmI, mmQ, mmO, 1, 3);
 fromRow = mmLastX / 16; fromCol = mmLastX & 7;
 toRow = mmLastY / 16;   toCol = mmLastY & 7;   // &7 also drops mmLastY's S/double-move flag bit
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
  bookReset();
  invalidateUndoSnapshot();

  tft.fillScreen(COLOR_BG);
  drawBoard(gs);
  drawNewGameButton();
  drawUndoButton();
  drawStatus("Your turn (White)");

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

  // New Game button: works in any game state (mid-game, game over, AI's
  // turn). Check before all other touch handling so a tap on the button
  // always wins. Wait for finger release with the same 2s timeout used
  // elsewhere so a stuck touch sensor can't freeze the reset.
  if (isTouchOnNewGameButton()) {
    delay(50); // debounce
    if (isTouchOnNewGameButton()) {
      unsigned long _waitStart = millis();
      while (touch.touched() && millis() - _waitStart < 2000) { delay(10); }
      resetGame();
      return;
    }
  }

  // Undo button: same "works in any game state" treatment as New Game,
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
        drawStatus("Your turn (White)");
      } else {
        drawStatus("Nothing to undo!");
        delay(800);
        drawStatus(gameOver ? "Game over" : "Your turn (White)");
      }
      return;
    }
  }

  if (gameOver) {
    // Wait for touch to restart
    if (touch.touched()) {
      delay(300);
      // Wait for release — bail after 2s in case the sensor glitches and
      // never reports !touched(). Without the timeout, a stuck touch event
      // would freeze the game (loop() would never return, so we'd stop
      // processing input even though the WDT stays fed by delay(10)).
      unsigned long _waitStart = millis();
      while (touch.touched() && millis() - _waitStart < 2000) { delay(10); }
      gameOver = false;
      pieceSelected = false;
      selectedRow = -1;
      selectedCol = -1;
      legalMoveCount = 0;
      initBoard(gs);
      microMaxInit();
      bookReset();
      invalidateUndoSnapshot();
      tft.fillScreen(COLOR_BG);
      drawBoard(gs);
      drawNewGameButton();
      drawUndoButton();
      drawStatus("Your turn (White)");
    }
    return;
  }

  if (gs.currentPlayer == WHITE_PIECE) {
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
      if (gs.board[tRow][tCol] != EMPTY && gs.color[tRow][tCol] == WHITE_PIECE) {
        selectedRow = tRow;
        selectedCol = tCol;
        pieceSelected = true;
        // Generate legal moves for this piece
        Move allMoves[256];
        int allCount = 0;
        generateMoves(gs, WHITE_PIECE, allMoves, allCount);
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
        drawStatus("Your turn (White)");
        return;
      }

      // Re-select another white piece
      if (gs.board[tRow][tCol] != EMPTY && gs.color[tRow][tCol] == WHITE_PIECE) {
        selectedRow = tRow;
        selectedCol = tCol;
        Move allMoves[256];
        int allCount = 0;
        generateMoves(gs, WHITE_PIECE, allMoves, allCount);
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
        drawStatus("Your turn (White)");
        return;
      }

      // Check game end conditions after player move
      drawBoard(gs);

      if (isCheckmate(gs, BLACK_PIECE)) {
        drawStatus("Checkmate! You win!");
        drawGameOver("You Win!");
        gameOver = true;
        return;
      }
      if (isStalemate(gs, BLACK_PIECE)) {
        drawStatus("Stalemate!");
        drawGameOver("Stalemate!");
        gameOver = true;
        return;
      }
      if (isInCheck(gs, BLACK_PIECE)) {
        drawStatus("Check! AI thinking...");
      } else {
        drawStatus("AI thinking...");
      }

    }
  } else {
    // AI's turn (Black)
    delay(100);

    Move allMoves[256];
    int allCount = 0;
    generateMoves(gs, BLACK_PIECE, allMoves, allCount);

    // Trust the book/micro-Max only as far as this sketch's own legal-move
    // list confirms -- match the chosen from/to squares against a move this
    // sketch already knows is legal, so promotion/en-passant/castling are
    // always applied via this sketch's own (already-tested) logic rather
    // than needing to trust either source's board state directly.
    Move best = allMoves[0]; // emergency fallback if no match is ever found
    bool found = false;

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
        Serial.printf("[book] chose (%d,%d)->(%d,%d), not in this sketch's own legal moves -- falling back to micro-Max\n",
                      bkFromRow, bkFromCol, bkToRow, bkToCol);
      }
    }

    if (!found) {
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
        Serial.printf("[micro-Max] chose (%d,%d)->(%d,%d), not in this sketch's own legal moves -- falling back\n",
                      mmFromRow, mmFromCol, mmToRow, mmToCol);
      }
    }

    bookRecordMove(best.fromRow, best.fromCol, best.toRow, best.toCol);

    applyMove(gs, best);

    drawBoard(gs);

    if (isCheckmate(gs, WHITE_PIECE)) {
      drawStatus("Checkmate! AI wins!");
      drawGameOver("AI Wins!");
      gameOver = true;
      return;
    }
    if (isStalemate(gs, WHITE_PIECE)) {
      drawStatus("Stalemate!");
      drawGameOver("Stalemate!");
      gameOver = true;
      return;
    }
    if (isInCheck(gs, WHITE_PIECE)) {
      drawStatus("You're in Check!");
    } else {
      drawStatus("Your turn (White)");
    }
  }
}
