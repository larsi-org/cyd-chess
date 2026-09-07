// Pure chess rules for cyd-chess: piece/board representation, move
// generation (castling/en-passant/promotion included), and
// check/checkmate/stalemate detection. No display or touch dependency at
// all -- everything here operates only on GameState/Move, so this is usable
// (and used, by the off-device test harnesses) completely independent of
// the ESP32/TFT_eSPI/touch side of the sketch. See chess_rules.cpp for the
// implementation.
#ifndef CYD_CHESS_RULES_H
#define CYD_CHESS_RULES_H

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

struct Move {
  int fromRow, fromCol, toRow, toCol;
  bool promotion;
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

void initBoard(GameState &gs);
bool inBounds(int r, int c);
bool squareAttacked(GameState &gs, int row, int col, int byPlayer);
bool isInCheck(GameState &gs, int player);
void addMove(Move *moves, int &count, int fr, int fc, int tr, int tc, GameState &gs, bool enPassant = false, bool castling = false, int rookFromCol = -1, int rookToCol = -1);
void generatePseudoMoves(GameState &gs, int player, Move *moves, int &count);
void applyMove(GameState &gs, Move &m);
void generateMoves(GameState &gs, int player, Move *moves, int &count);
bool isCheckmate(GameState &gs, int player);
bool isStalemate(GameState &gs, int player);

#endif // CYD_CHESS_RULES_H
