// Chess rules implementation for cyd-chess -- see chess_rules.h for the
// public interface. Deliberately has no display or touch dependency; the
// main .ino's drawing/touch-handling code is the only part of this sketch
// that knows it's running on an ESP32 with a touchscreen at all.
#include <Arduino.h> // memset, abs
#include "chess_rules.h"

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
  m.promotion = false;
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

void generateMoves(GameState &gs, int player, Move *moves, int &count) {
  count = 0;
  Move pseudoMoves[256];
  int pseudoCount = 0;
  generatePseudoMoves(gs, player, pseudoMoves, pseudoCount);

  // Snapshot/restore the full GameState rather than a partial move-undo —
  // restoring just board[][]/color[][] after a pseudo-move leaves enPassant
  // target, castling rights, and currentPlayer corrupted. With those leaking
  // across pseudoMove iterations, the AI sees positions that can't actually
  // arise and emits illegal moves (e.g. rook capturing its own pawn). Full
  // copy costs ~520 bytes per iteration but eliminates the whole class of bugs.
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

