// Chess game for ESP32-2432S028R with ILI9341 TFT display
// Player (White) vs AI (Black) using minimax with alpha-beta pruning
// Touch screen for piece selection and movement

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
int pieceValue(int piece);
int getPST(int piece, int row, int col, int color);
int evaluateBoard(GameState &gs);
int minimax(GameState &gs, int depth, int alpha, int beta, bool maximizing);
Move getBestMove(GameState &gs);
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
void drawSquare(int row, int col, bool highlight, bool moveDot, GameState &gs);
void drawPiece(int row, int col, int piece, int pieceColor);
bool isValidMove(GameState &gs, Move &m);
void generateMoves(GameState &gs, int player, Move *moves, int &count);
void applyMove(GameState &gs, Move &m);
void undoMove(GameState &gs, Move &m);
bool isInCheck(GameState &gs, int player);
bool isCheckmate(GameState &gs, int player);
bool isStalemate(GameState &gs, int player);
int evaluateBoard(GameState &gs);
int minimax(GameState &gs, int depth, int alpha, int beta, bool maximizing);
Move getBestMove(GameState &gs);
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

// Piece glyphs -- 24x24 1-bit bitmaps (3 bytes/row, MSB first), lifted from
// Sergey Urusov's Arduino Mega Chess II (github.com/m4k3r-org/M5Stack-MegaChess,
// a straight port of the same gui.h graphics). PIECE_FILL is the solid
// silhouette; PIECE_OUTLINE is a second bitmap of the same shape's interior
// line detail (mane, crenellations, cross, etc.), drawn on top in a
// contrasting color -- two flat fills stacked, no grayscale/anti-aliasing.
// Indexed piece-1 (PAWN=1..KING=6 -> 0..5), matching this sketch's own
// piece-type numbering, not just Urusov's fp..fk order.
#define PIECE_SIZE 24
const uint8_t PIECE_FILL[6][72] PROGMEM = {
  { // Pawn
    0x0, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x7E, 0x0,  0x0,  0xFF, 0x0,
    0x0,  0xFF, 0x0,  0x0,  0xFF, 0x0,  0x0,  0x7E, 0x0,  0x0,  0x3C, 0x0,  0x1,  0xFF, 0x80, 0x1,  0xFF, 0x80,
    0x0,  0x3C, 0x0,  0x0,  0x3C, 0x0,  0x0,  0x7E, 0x0,  0x0,  0x7E, 0x0,  0x0,  0xFF, 0x0,  0x1,  0xFF, 0x80,
    0x3,  0xFF, 0xC0, 0x7,  0xFF, 0xE0, 0x7,  0xFF, 0xE0, 0x7,  0xFF, 0xE0, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0
  },
  { // Knight
    0x0, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x66, 0x0,  0x0,  0xFF, 0x0,  0x1,  0xFF, 0x80, 0x3,  0xFF, 0xC0,
    0x7,  0xFF, 0xE0, 0xF,  0xFF, 0xF0, 0x1F, 0xFF, 0xF8, 0x1F, 0x3F, 0xF8, 0x1E, 0x3F, 0xF8, 0xC,  0x7F, 0xF8,
    0x0,  0xFF, 0xF0, 0x1,  0xFF, 0xE0, 0x1,  0xFF, 0xC0, 0x1,  0xFF, 0x80, 0x1,  0xFF, 0x0,  0x0,  0xFE, 0x0,
    0x0,  0x7E, 0x0,  0x7,  0xFF, 0xE0, 0xF,  0xFF, 0xF0, 0xF,  0xFF, 0xF0, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0
  },
  { // Bishop
    0x0, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x3C, 0x0,  0x0,  0x7E, 0x0,  0x0,  0x7E, 0x0,  0x0,  0x3C, 0x0,
    0x1,  0xFF, 0x80, 0x3,  0xFF, 0xC0, 0x7,  0xFF, 0xE0, 0x7,  0xFF, 0xE0, 0x7,  0xFF, 0xE0, 0x7,  0xFF, 0xE0,
    0x7,  0xFF, 0xE0, 0x7,  0xFF, 0xE0, 0x7,  0xFF, 0xE0, 0x3,  0xFF, 0xC0, 0x1,  0xFF, 0x80, 0x0,  0xFF, 0x0,
    0x8,  0x7E, 0x10, 0x1C, 0x7E, 0x38, 0x3F, 0xFF, 0xFC, 0x3F, 0xFF, 0xFC, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0
  },
  { // Rook
    0x0, 0x0,  0x0,  0x0,  0x0,  0x0,  0xF,  0x3C, 0xF0, 0xF,  0x3C, 0xF0, 0xF,  0xFF, 0xF0, 0xF,  0xFF, 0xF0,
    0x7,  0xFF, 0xE0, 0x3,  0xFF, 0xC0, 0x3,  0xFF, 0xC0, 0x3,  0xFF, 0xC0, 0x3,  0xFF, 0xC0, 0x3,  0xFF, 0xC0,
    0x3,  0xFF, 0xC0, 0x3,  0xFF, 0xC0, 0x3,  0xFF, 0xC0, 0x3,  0xFF, 0xC0, 0x3,  0xFF, 0xC0, 0x3,  0xFF, 0xC0,
    0x3,  0xFF, 0xC0, 0x7,  0xFF, 0xE0, 0xF,  0xFF, 0xF0, 0xF,  0xFF, 0xF0, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0
  },
  { // Queen
    0x0, 0x0,  0x0,  0x0,  0x18, 0x0,  0x3,  0x3C, 0xC0, 0x7,  0x99, 0xE0, 0x33, 0x18, 0xCC, 0x7B, 0x18, 0xDE,
    0x33, 0x18, 0xCC, 0x33, 0x18, 0xCC, 0x33, 0x18, 0xCC, 0x33, 0x18, 0xCC, 0x33, 0x18, 0xCC, 0x33, 0xBD, 0xCC,
    0x1B, 0xBD, 0xD8, 0x1F, 0xFF, 0xF8, 0x1F, 0xFF, 0xF8, 0xF,  0xFF, 0xF0, 0xF,  0xFF, 0xF0, 0x7,  0xFF, 0xE0,
    0x3,  0xFF, 0xC0, 0x7,  0xFF, 0xE0, 0xF,  0xFF, 0xF0, 0xF,  0xFF, 0xF0, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0
  },
  { // King
    0x0, 0x0,  0x0,  0x0,  0x3C, 0x0,  0x0,  0x3C, 0x0,  0x0,  0x3C, 0x0,  0xF,  0x3C, 0xF0, 0x1F, 0xFF, 0xF8,
    0x3F, 0xFF, 0xFC, 0x7F, 0xFF, 0xFE, 0x7F, 0xFF, 0xFE, 0x7F, 0xFF, 0xFE, 0x7F, 0xFF, 0xFE, 0x7F, 0xFF, 0xFE,
    0x7F, 0xFF, 0xFE, 0x7F, 0xFF, 0xFE, 0x3F, 0xFF, 0xFC, 0x1F, 0xFF, 0xF8, 0xF,  0xFF, 0xF0, 0x7,  0xFF, 0xE0,
    0x3,  0xFF, 0xC0, 0x7,  0xFF, 0xE0, 0xF,  0xFF, 0xF0, 0xF,  0xFF, 0xF0, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0
  }
};
const uint8_t PIECE_OUTLINE[6][72] PROGMEM = {
  { // Pawn
    0x0, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x7E, 0x0,  0x0,  0x81, 0x0,  0x1,  0x0,  0x80,
    0x1,  0x0,  0x80, 0x1,  0x0,  0x80, 0x0,  0x81, 0x0,  0x3,  0xC3, 0xC0, 0x2,  0x0,  0x40, 0x2,  0x0,  0x40,
    0x3,  0xC3, 0xC0, 0x0,  0x42, 0x0,  0x0,  0x81, 0x0,  0x0,  0x81, 0x0,  0x1,  0x0,  0x80, 0x2,  0x0,  0x40,
    0x4,  0x0,  0x20, 0x8,  0x0,  0x10, 0x8,  0x0,  0x10, 0x8,  0x0,  0x10, 0xF,  0xFF, 0xF0, 0x0,  0x0,  0x0
  },
  { // Knight
    0x0, 0x0,  0x0,  0x0,  0x66, 0x0,  0x0,  0x99, 0x0,  0x1,  0x0,  0x80, 0x2,  0xC0, 0x40, 0x4,  0xC0, 0x20,
    0x8,  0x0,  0x10, 0x10, 0x0,  0x8,  0x20, 0x0,  0x4,  0x20, 0xC0, 0x4,  0x21, 0x40, 0x4,  0x12, 0x80, 0x4,
    0xD,  0x0,  0x8,  0x2,  0x0,  0x10, 0x2,  0x0,  0x20, 0x2,  0x0,  0x40, 0x2,  0x0,  0x80, 0x1,  0x1,  0x0,
    0x7,  0x81, 0xE0, 0x8,  0x0,  0x10, 0x10, 0x0,  0x8,  0x10, 0x0,  0x8,  0x1F, 0xFF, 0xF8, 0x0,  0x0,  0x0
  },
  { // Bishop
    0x0, 0x0,  0x0,  0x0,  0x3C, 0x0,  0x0,  0x42, 0x0,  0x0,  0x81, 0x0,  0x0,  0x81, 0x0,  0x1,  0xC3, 0x80,
    0x2,  0x18, 0x40, 0x4,  0x18, 0x20, 0x8,  0x18, 0x10, 0x8,  0x18, 0x10, 0x9,  0xFF, 0x90, 0x9,  0xFF, 0x90,
    0x8,  0x18, 0x10, 0x8,  0x18, 0x10, 0x8,  0x18, 0x10, 0x4,  0x18, 0x20, 0x2,  0x18, 0x40, 0x9,  0x0,  0x90,
    0x14, 0x81, 0x28, 0x23, 0x81, 0xC4, 0x40, 0x0,  0x2,  0x40, 0x0,  0x2,  0x7F, 0xFF, 0xFE, 0x0,  0x0,  0x0
  },
  { // Rook
    0x0, 0x0,  0x0,  0x1F, 0x3C, 0xF8, 0x10, 0xC3, 0x8,  0x10, 0xC3, 0x8,  0x10, 0x0,  0x8,  0x10, 0x0,  0x8,
    0xB,  0xFF, 0xD0, 0x4,  0x0,  0x20, 0x4,  0x0,  0x20, 0x4,  0x0,  0x20, 0x4,  0x0,  0x20, 0x4,  0x0,  0x20,
    0x4,  0x0,  0x20, 0x4,  0x0,  0x20, 0x4,  0x0,  0x20, 0x4,  0x0,  0x20, 0x4,  0x0,  0x20, 0x4,  0x0,  0x20,
    0x4,  0x0,  0x20, 0xB,  0xFF, 0xD0, 0x10, 0x0,  0x8,  0x10, 0x0,  0x8,  0x1F, 0xFF, 0xF8, 0x0,  0x0,  0x0
  },
  { // Queen
    0x0, 0x18, 0x0,  0x3,  0x24, 0xC0, 0x4,  0xC3, 0x20, 0x38, 0x66, 0x1C, 0x4C, 0xA5, 0x32, 0x84, 0xA5, 0x21,
    0x4C, 0xA5, 0x32, 0x4C, 0xA5, 0x32, 0x4C, 0xA5, 0x32, 0x4C, 0xA5, 0x32, 0x4C, 0xA5, 0x32, 0x4C, 0x42, 0x32,
    0x24, 0x42, 0x24, 0x20, 0x0,  0x4,  0x20, 0x0,  0x4,  0x10, 0x0,  0x8,  0x10, 0x0,  0x8,  0x8,  0x0,  0x10,
    0x4,  0x0,  0x20, 0xF,  0xFF, 0xF0, 0x10, 0x0,  0x8,  0x10, 0x0,  0x8,  0x1F, 0xFF, 0xF8, 0x0,  0x0,  0x0
  },
  { // King
    0x0, 0x7E, 0x0,  0x0,  0x42, 0x0,  0x0,  0x42, 0x0,  0xF,  0xC3, 0xF0, 0x10, 0xC3, 0x8,  0x20, 0x7E, 0x4,
    0x40, 0x3C, 0x2,  0x80, 0x18, 0x1,  0x80, 0x18, 0x1,  0x80, 0x18, 0x1,  0x80, 0x18, 0x1,  0x80, 0x18, 0x1,
    0x80, 0x18, 0x1,  0x80, 0x18, 0x1,  0x40, 0x18, 0x2,  0x20, 0x18, 0x4,  0x10, 0x18, 0x8,  0x8,  0x18, 0x10,
    0x4,  0x18, 0x20, 0xB,  0xFF, 0xD0, 0x10, 0x0,  0x8,  0x10, 0x0,  0x8,  0x1F, 0xFF, 0xF8, 0x0,  0x0,  0x0
  }
};

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

// New Game button — bottom of screen, below the chess board.
// 240×320 portrait: board occupies y=40..280; column labels at y=282..290;
// button gets the remaining strip y=294..318.
#define NEW_GAME_BTN_X      60
#define NEW_GAME_BTN_Y     294
#define NEW_GAME_BTN_W     120
#define NEW_GAME_BTN_H      24

void drawNewGameButton() {
  tft.fillRoundRect(NEW_GAME_BTN_X, NEW_GAME_BTN_Y, NEW_GAME_BTN_W, NEW_GAME_BTN_H, 4, TFT_DARKGREEN);
  tft.drawRoundRect(NEW_GAME_BTN_X, NEW_GAME_BTN_Y, NEW_GAME_BTN_W, NEW_GAME_BTN_H, 4, TFT_GREEN);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
  // Centred-ish text — TFT_eSPI default font is ~6 pixels per character.
  tft.setCursor(NEW_GAME_BTN_X + (NEW_GAME_BTN_W - 8 * 6) / 2, NEW_GAME_BTN_Y + (NEW_GAME_BTN_H - 8) / 2);
  tft.print("NEW GAME");
}

bool isTouchOnNewGameButton() {
  if (!touch.touched()) return false;
  TS_Point p = touch.getPoint();
  int tx = map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, 240);
  int ty = map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, 320);
  return (tx >= NEW_GAME_BTN_X && tx < NEW_GAME_BTN_X + NEW_GAME_BTN_W &&
          ty >= NEW_GAME_BTN_Y && ty < NEW_GAME_BTN_Y + NEW_GAME_BTN_H);
}

void resetGame() {
  initBoard(gs);
  pieceSelected = false;
  selectedRow = -1;
  selectedCol = -1;
  legalMoveCount = 0;
  gameOver = false;
  tft.fillScreen(COLOR_BG);
  drawBoard(gs);
  drawNewGameButton();
  drawStatus("Your turn (White)");
}

void drawBoard(GameState &gs) {
  tft.fillRect(BOARD_OFFSET_X, BOARD_OFFSET_Y, 8 * SQUARE_SIZE, 8 * SQUARE_SIZE, COLOR_LIGHT_SQ);

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

// ─── Evaluation ───────────────────────────────────────────────────────────
int pieceValue(int piece) {
  switch (piece) {
    case PAWN:   return 100;
    case KNIGHT: return 320;
    case BISHOP: return 330;
    case ROOK:   return 500;
    case QUEEN:  return 900;
    case KING:   return 20000;
    default:     return 0;
  }
}

// Piece-square tables (from white's perspective)
const int pawnTable[8][8] = {
  { 0,  0,  0,  0,  0,  0,  0,  0},
  {50, 50, 50, 50, 50, 50, 50, 50},
  {10, 10, 20, 30, 30, 20, 10, 10},
  { 5,  5, 10, 25, 25, 10,  5,  5},
  { 0,  0,  0, 20, 20,  0,  0,  0},
  { 5, -5,-10,  0,  0,-10, -5,  5},
  { 5, 10, 10,-20,-20, 10, 10,  5},
  { 0,  0,  0,  0,  0,  0,  0,  0}
};

const int knightTable[8][8] = {
  {-50,-40,-30,-30,-30,-30,-40,-50},
  {-40,-20,  0,  0,  0,  0,-20,-40},
  {-30,  0, 10, 15, 15, 10,  0,-30},
  {-30,  5, 15, 20, 20, 15,  5,-30},
  {-30,  0, 15, 20, 20, 15,  0,-30},
  {-30,  5, 10, 15, 15, 10,  5,-30},
  {-40,-20,  0,  5,  5,  0,-20,-40},
  {-50,-40,-30,-30,-30,-30,-40,-50}
};

const int bishopTable[8][8] = {
  {-20,-10,-10,-10,-10,-10,-10,-20},
  {-10,  0,  0,  0,  0,  0,  0,-10},
  {-10,  0,  5, 10, 10,  5,  0,-10},
  {-10,  5,  5, 10, 10,  5,  5,-10},
  {-10,  0, 10, 10, 10, 10,  0,-10},
  {-10, 10, 10, 10, 10, 10, 10,-10},
  {-10,  5,  0,  0,  0,  0,  5,-10},
  {-20,-10,-10,-10,-10,-10,-10,-20}
};

int getPST(int piece, int row, int col, int color) {
  int r = (color == WHITE_PIECE) ? row : (7 - row);
  switch (piece) {
    case PAWN:   return pawnTable[r][col];
    case KNIGHT: return knightTable[r][col];
    case BISHOP: return bishopTable[r][col];
    default:     return 0;
  }
}

int evaluateBoard(GameState &gs) {
  int score = 0;
  for (int r = 0; r < 8; r++) {
    for (int c = 0; c < 8; c++) {
      if (gs.board[r][c] == EMPTY) continue;
      int v = pieceValue(gs.board[r][c]) + getPST(gs.board[r][c], r, c, gs.color[r][c]);
      if (gs.color[r][c] == BLACK_PIECE) score += v;
      else score -= v;
    }
  }
  return score;
}

// ─── Minimax with Alpha-Beta ───────────────────────────────────────────────
// Stack-overflow fix: a per-frame `Move moves[256]` (~10KB) × recursion depth
// blows past the loopTask stack (even at 32KB) when combined with the
// setup→loop→handleTouch→getBestMove call chain. Loop task is single-threaded
// so a per-depth global pool is safe — each recursion level reads/writes its
// own slot.
#define MINIMAX_MAX_DEPTH 6
static Move minimaxMoveBuf[MINIMAX_MAX_DEPTH + 1][256];

int minimax(GameState &gs, int depth, int alpha, int beta, bool maximizing) {
  if (depth == 0) return evaluateBoard(gs);

  int player = maximizing ? BLACK_PIECE : WHITE_PIECE;
  Move *moves = (depth >= 0 && depth <= MINIMAX_MAX_DEPTH) ? minimaxMoveBuf[depth] : minimaxMoveBuf[0];
  int count = 0;
  generateMoves(gs, player, moves, count);

  if (count == 0) {
    if (isInCheck(gs, player)) {
      return maximizing ? -30000 - depth : 30000 + depth;
    }
    return 0; // stalemate
  }

  // vTaskDelay(1) (= 1 tick) actually lets the IDLE task run, unlike yield()
  // which only schedules equal-or-higher priority. Calling every node would
  // be too slow (~1ms × thousands of nodes); call every 16th instead.
  static uint32_t s_minimaxNodeCounter = 0;

  if (maximizing) {
    int maxEval = -32767;
    for (int i = 0; i < count; i++) {
      GameState saved = gs;
      applyMove(gs, moves[i]);
      int eval = minimax(gs, depth - 1, alpha, beta, false);
      gs = saved;
      if ((++s_minimaxNodeCounter & 0x0F) == 0) vTaskDelay(1);
      if (eval > maxEval) maxEval = eval;
      if (eval > alpha) alpha = eval;
      if (beta <= alpha) break;
    }
    return maxEval;
  } else {
    int minEval = 32767;
    for (int i = 0; i < count; i++) {
      GameState saved = gs;
      applyMove(gs, moves[i]);
      int eval = minimax(gs, depth - 1, alpha, beta, true);
      gs = saved;
      if ((++s_minimaxNodeCounter & 0x0F) == 0) vTaskDelay(1);
      if (eval < minEval) minEval = eval;
      if (eval < beta) beta = eval;
      if (beta <= alpha) break;
    }
    return minEval;
  }
}

Move getBestMove(GameState &gs) {
  Move moves[256];
  int count = 0;
  generateMoves(gs, BLACK_PIECE, moves, count);

  Move bestMove = moves[0];
  int bestVal = -32767;
  int depth = 2; // Search depth — reduced from 3 to keep AI move time
                 // around 1s on ESP32 240MHz. Still a credible opponent.

  for (int i = 0; i < count; i++) {
    GameState saved = gs;
    applyMove(gs, moves[i]);
    int val = minimax(gs, depth - 1, -32767, 32767, false);
    gs = saved;
    if (val > bestVal) {
      bestVal = val;
      bestMove = moves[i];
    }
  }
  return bestMove;
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

  tft.fillScreen(COLOR_BG);
  drawBoard(gs);
  drawNewGameButton();
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
      tft.fillScreen(COLOR_BG);
      drawBoard(gs);
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
        drawBoard(gs);
        drawStatus("Select destination");
      }
    } else {
      // Deselect if tapping same square
      if (tRow == selectedRow && tCol == selectedCol) {
        pieceSelected = false;
        selectedRow = -1;
        selectedCol = -1;
        legalMoveCount = 0;
        drawBoard(gs);
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
        drawBoard(gs);
        drawStatus("Select destination");
        return;
      }

      // Try to make a move
      bool moveMade = false;
      for (int i = 0; i < legalMoveCount; i++) {
        if (legalMoves[i].toRow == tRow && legalMoves[i].toCol == tCol) {
          applyMove(gs, legalMoves[i]);
          moveMade = true;
          break;
        }
      }

      pieceSelected = false;
      selectedRow = -1;
      selectedCol = -1;
      legalMoveCount = 0;

      if (!moveMade) {
        drawBoard(gs);
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

    Move best = getBestMove(gs);
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
