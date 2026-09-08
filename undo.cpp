// Single-level undo for cyd-chess -- see undo.h for the public interface.
//
// One saved snapshot, taken right before the human's move is applied --
// restoring it always reverts to "right before my last move", which also
// erases whatever the AI replied with in between (there's no sensible way
// to undo only the AI's reply and leave the human's own move in place, and
// a kid using this to take back a blunder wants the whole exchange gone
// anyway). No stack, no redo -- exactly one level, by design, per Lars.
//
// Three separate pieces of state have to travel together or the AI and
// this sketch's own board desync: this sketch's own GameState, micro-Max's
// own board/running state (it never sees an undo otherwise -- it would
// still think the undone moves happened), and the opening book's move
// history (same reason -- otherwise it could offer a book move that no
// longer matches what's actually on the board). The draw-detection
// position history travels too, for the same reason.
#include <Arduino.h> // memcpy
#include "undo.h"
#include "micromax.h"
#include "book.h"

struct UndoSnapshot {
  bool valid;
  GameState gs;
  signed char mmB[129];
  int mmJ, mmZ, mmk, mmR, mmQ, mmO;
  int plyCount;
  bool outOfBook;
  BookMove moveHistory[BOOK_MAX_PLY];
  uint32_t positionHistory[MAX_POSITION_HISTORY];
  int positionHistoryCount;
};
UndoSnapshot undoSnapshot = { false };

void invalidateUndoSnapshot() {
  undoSnapshot.valid = false;
}

void saveUndoSnapshot(GameState &g) {
  undoSnapshot.valid = true;
  undoSnapshot.gs = g; // halfmoveClock rides along inside GameState for free
  memcpy(undoSnapshot.mmB, mmB, sizeof(mmB));
  undoSnapshot.mmJ = mmJ; undoSnapshot.mmZ = mmZ; undoSnapshot.mmk = mmk;
  undoSnapshot.mmR = mmR; undoSnapshot.mmQ = mmQ; undoSnapshot.mmO = mmO;
  undoSnapshot.plyCount = plyCount;
  undoSnapshot.outOfBook = outOfBook;
  memcpy(undoSnapshot.moveHistory, moveHistory, sizeof(moveHistory));
  memcpy(undoSnapshot.positionHistory, positionHistory, sizeof(positionHistory));
  undoSnapshot.positionHistoryCount = positionHistoryCount;
}

bool restoreUndoSnapshot(GameState &g) {
  if (!undoSnapshot.valid) return false;
  g = undoSnapshot.gs;
  memcpy(mmB, undoSnapshot.mmB, sizeof(mmB));
  mmJ = undoSnapshot.mmJ; mmZ = undoSnapshot.mmZ; mmk = undoSnapshot.mmk;
  mmR = undoSnapshot.mmR; mmQ = undoSnapshot.mmQ; mmO = undoSnapshot.mmO;
  plyCount = undoSnapshot.plyCount;
  outOfBook = undoSnapshot.outOfBook;
  memcpy(moveHistory, undoSnapshot.moveHistory, sizeof(moveHistory));
  memcpy(positionHistory, undoSnapshot.positionHistory, sizeof(positionHistory));
  positionHistoryCount = undoSnapshot.positionHistoryCount;
  undoSnapshot.valid = false; // one level only -- used up until the next move
  return true;
}
