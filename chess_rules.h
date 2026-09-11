// chess_rules.h
// MIT License
// https://opensource.org/licenses/MIT
// Copyright (c) 2026, Lars Schumann, larsi.org@gmail.com
//
// Pure chess rules for cyd-chess: piece/board representation, move
// generation (castling/en-passant/promotion included), and
// check/checkmate/stalemate detection. No display or touch dependency at
// all -- everything here operates only on GameState/Move, so this is usable
// (and used, by the off-device test harnesses) completely independent of
// the ESP32/TFT_eSPI/touch side of the sketch. See chess_rules.cpp for the
// implementation.
#ifndef CYD_CHESS_RULES_H
#define CYD_CHESS_RULES_H

#include <stdint.h> // uint32_t (positionHistory[], below)

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
  int promoteTo;  // which piece a promotion becomes -- QUEEN unless the
                  // caller (the human's own move, via the promotion-choice
                  // screen) overrides it; addMove() defaults every move to
                  // QUEEN, so AI/book moves (which never touch this field)
                  // keep auto-queening exactly as before this field existed.
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
  int halfmoveClock;      // half-moves since the last pawn move/capture --
                          // 50-move-rule draw at 100 (see isDrawByFiftyMoveRule)
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

// ─── Draw detection beyond stalemate ───────────────────────────────────────
// Threefold repetition needs a history of positions seen this game. Kept as
// a side-table here -- like the opening book's own moveHistory[]/plyCount --
// rather than inside GameState itself: GameState already gets deep-copied
// once per pseudo-move inside generateMoves()'s own legality check (see
// that function's comment on the cost of doing so), and a growing history
// array only ever needs to grow when a move is actually committed to the
// real game, never during legality scanning -- putting it in GameState
// would multiply that hot copy's cost for no benefit.
#define MAX_POSITION_HISTORY 600 // generous cap -- a real game rarely runs this long
extern uint32_t positionHistory[MAX_POSITION_HISTORY];
extern int positionHistoryCount;
void resetPositionHistory();
void recordPosition(GameState &gs); // call once per move actually committed to the game

bool isDrawByRepetition(GameState &gs);        // current position seen 3+ times
bool isDrawByFiftyMoveRule(GameState &gs);     // halfmoveClock >= 100
bool isDrawByInsufficientMaterial(GameState &gs); // bare kings, or king + lone minor vs. bare king
bool isDraw(GameState &gs); // any of the above -- what callers actually want

#endif // CYD_CHESS_RULES_H
