// Opening book for cyd-chess -- see book.h for what's exposed to the rest of
// the sketch (BookMove/BOOK_MAX_PLY, and the moveHistory/plyCount/outOfBook
// globals this sketch's own undo snapshot has to reach directly). Everything
// else here, including BOOK_LINES itself, is private to this file.
#include <Arduino.h> // esp_random()
#include "book.h"

// ─── Opening Book ───────────────────────────────────────────────────────────
// A small set of named lines from lichess-org/chess-openings (CC0), covering
// principled replies to White's common first moves. Bypasses micro-Max's
// search entirely while the game's own move history still matches a line's
// prefix -- the book's next move is cross-checked against this sketch's own
// legal-move list the same way a micro-Max move is (see loop()'s AI-turn
// branch), and applied to *both* this sketch's own board and micro-Max's
// via microMaxApplyMove(), so the two stay in lockstep exactly as if
// micro-Max had searched it out itself.
//
// Each line's moves are this sketch's own (row,col) coordinates (row 0 =
// rank 8/Black's back row, matching this sketch's board layout throughout --
// col = file 'a'..'h' as 0..7, row = 8 - rank), traced by hand from the
// standard starting position against each line's SAN in the source dataset.
// BOOK_NONE terminates a line short of BOOK_MAX_PLY.
#define BOOK_LINE_COUNT (sizeof(BOOK_LINES) / sizeof(BOOK_LINES[0]))


const BookMove BOOK_LINES[][BOOK_MAX_PLY] = {
  // Ruy Lopez (C60): 1. e4 e5 2. Nf3 Nc6 3. Bb5
  { {6,4,4,4}, {1,4,3,4}, {7,6,5,5}, {0,1,2,2}, {7,5,3,1}, {BOOK_NONE,0,0,0} },
  // Italian Game (C50): 1. e4 e5 2. Nf3 Nc6 3. Bc4
  { {6,4,4,4}, {1,4,3,4}, {7,6,5,5}, {0,1,2,2}, {7,5,4,2}, {BOOK_NONE,0,0,0} },
  // Scotch Game (C44): 1. e4 e5 2. Nf3 Nc6 3. d4
  { {6,4,4,4}, {1,4,3,4}, {7,6,5,5}, {0,1,2,2}, {6,3,4,3}, {BOOK_NONE,0,0,0} },
  // Vienna Game: Anderssen Defense (C25): 1. e4 e5 2. Nc3 Bc5
  { {6,4,4,4}, {1,4,3,4}, {7,1,5,2}, {0,5,3,2}, {BOOK_NONE,0,0,0}, {BOOK_NONE,0,0,0} },
  // Petrov's Defense (C42): 1. e4 e5 2. Nf3 Nf6
  { {6,4,4,4}, {1,4,3,4}, {7,6,5,5}, {0,6,2,5}, {BOOK_NONE,0,0,0}, {BOOK_NONE,0,0,0} },
  // Sicilian Defense (B50): 1. e4 c5 2. Nf3 d6 3. d4
  { {6,4,4,4}, {1,2,3,2}, {7,6,5,5}, {1,3,2,3}, {6,3,4,3}, {BOOK_NONE,0,0,0} },
  // French Defense (C00): 1. e4 e6 2. d4 d5
  { {6,4,4,4}, {1,4,2,4}, {6,3,4,3}, {1,3,3,3}, {BOOK_NONE,0,0,0}, {BOOK_NONE,0,0,0} },
  // Caro-Kann Defense (B12): 1. e4 c6 2. d4 d5
  { {6,4,4,4}, {1,2,2,2}, {6,3,4,3}, {1,3,3,3}, {BOOK_NONE,0,0,0}, {BOOK_NONE,0,0,0} },
  // Queen's Gambit Declined (D30): 1. d4 d5 2. c4 e6
  { {6,3,4,3}, {1,3,3,3}, {6,2,4,2}, {1,4,2,4}, {BOOK_NONE,0,0,0}, {BOOK_NONE,0,0,0} },
  // Slav Defense (D10): 1. d4 d5 2. c4 c6
  { {6,3,4,3}, {1,3,3,3}, {6,2,4,2}, {1,2,2,2}, {BOOK_NONE,0,0,0}, {BOOK_NONE,0,0,0} },
  // Queen's Gambit Accepted (D20): 1. d4 d5 2. c4 dxc4
  { {6,3,4,3}, {1,3,3,3}, {6,2,4,2}, {3,3,4,2}, {BOOK_NONE,0,0,0}, {BOOK_NONE,0,0,0} },
  // King's Indian Defense (E61): 1. d4 Nf6 2. c4 g6 3. Nc3
  { {6,3,4,3}, {0,6,2,5}, {6,2,4,2}, {1,6,2,6}, {7,1,5,2}, {BOOK_NONE,0,0,0} },
  // Nimzo-Indian Defense (E20): 1. d4 Nf6 2. c4 e6 3. Nc3 Bb4
  { {6,3,4,3}, {0,6,2,5}, {6,2,4,2}, {1,4,2,4}, {7,1,5,2}, {0,5,4,1} },
  // Réti Opening (A09): 1. Nf3 d5 2. c4
  { {7,6,5,5}, {1,3,3,3}, {6,2,4,2}, {BOOK_NONE,0,0,0}, {BOOK_NONE,0,0,0}, {BOOK_NONE,0,0,0} },
};
#define BOOK_LINE_COUNT (sizeof(BOOK_LINES) / sizeof(BOOK_LINES[0]))

BookMove moveHistory[BOOK_MAX_PLY];
int plyCount = 0;
bool outOfBook = false;

void bookReset() {
  plyCount = 0;
  outOfBook = false;
}

void bookRecordMove(int fromRow, int fromCol, int toRow, int toCol) {
  if (plyCount < BOOK_MAX_PLY) {
    moveHistory[plyCount] = {(signed char)fromRow, (signed char)fromCol, (signed char)toRow, (signed char)toCol};
  }
  plyCount++;
}

// Collects every line whose stored prefix exactly matches the game so far
// and that has a move at this exact ply, then picks uniformly at random
// among them via the ESP32's hardware RNG -- otherwise only the first
// matching line in table order would ever be reachable, permanently
// stranding every alternative reply to the same position.
bool bookGetMove(int &fromRow, int &fromCol, int &toRow, int &toCol) {
  if (outOfBook || plyCount >= BOOK_MAX_PLY) return false;

  const BookMove *candidates[BOOK_LINE_COUNT];
  int candidateCount = 0;

  for (int L = 0; L < (int)BOOK_LINE_COUNT; L++) {
    bool prefixMatches = true;
    for (int p = 0; p < plyCount; p++) {
      const BookMove &m = BOOK_LINES[L][p];
      if (m.fromRow == BOOK_NONE || m.fromRow != moveHistory[p].fromRow ||
          m.fromCol != moveHistory[p].fromCol || m.toRow != moveHistory[p].toRow ||
          m.toCol != moveHistory[p].toCol) {
        prefixMatches = false;
        break;
      }
    }
    if (prefixMatches && BOOK_LINES[L][plyCount].fromRow != BOOK_NONE) {
      const BookMove &cand = BOOK_LINES[L][plyCount];
      bool alreadyHave = false;
      for (int c = 0; c < candidateCount; c++) {
        if (candidates[c]->fromRow == cand.fromRow && candidates[c]->fromCol == cand.fromCol &&
            candidates[c]->toRow == cand.toRow && candidates[c]->toCol == cand.toCol) {
          alreadyHave = true;
          break;
        }
      }
      // Dedupe identical moves proposed by different lines sharing this
      // prefix -- several named lines happening to share the same reply at
      // this ply shouldn't bias selection toward it; each *distinct* move
      // gets equal weight, regardless of how many lines propose it.
      if (!alreadyHave) candidates[candidateCount++] = &cand;
    }
  }

  if (candidateCount == 0) {
    outOfBook = true; // no line matches what's been played -- stop checking for the rest of this game
    return false;
  }

  const BookMove &chosen = *candidates[esp_random() % candidateCount];
  fromRow = chosen.fromRow; fromCol = chosen.fromCol;
  toRow = chosen.toRow;     toCol = chosen.toCol;
  return true;
}

