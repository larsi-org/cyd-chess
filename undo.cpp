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
  int humanMoveCount; // move-rating's grace-period counter -- see undo.h
};
UndoSnapshot undoSnapshot = { false };

void invalidateUndoSnapshot() {
  undoSnapshot.valid = false;
}

void saveUndoSnapshot(GameState &g) {
  // Not just a latency nicety here -- mmB/etc. are read directly below, without going through
  // any of micromax.cpp's own three guarded entry points, so this is the one thing standing
  // between a still-running background eval (see micromax.h) and a torn read of a board it's
  // mid-search on.
  microMaxSyncBackgroundEval();
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
  undoSnapshot.humanMoveCount = humanMoveCount;
}

bool restoreUndoSnapshot(GameState &g) {
  if (!undoSnapshot.valid) return false;
  // Same reasoning as saveUndoSnapshot() above, and more likely to actually matter here: Undo is
  // typically tapped during the human's turn, right when the move-rating background eval for
  // the position they're now undoing is most likely still running.
  microMaxSyncBackgroundEval();
  g = undoSnapshot.gs;
  memcpy(mmB, undoSnapshot.mmB, sizeof(mmB));
  mmJ = undoSnapshot.mmJ; mmZ = undoSnapshot.mmZ; mmk = undoSnapshot.mmk;
  mmR = undoSnapshot.mmR; mmQ = undoSnapshot.mmQ; mmO = undoSnapshot.mmO;
  plyCount = undoSnapshot.plyCount;
  outOfBook = undoSnapshot.outOfBook;
  memcpy(moveHistory, undoSnapshot.moveHistory, sizeof(moveHistory));
  memcpy(positionHistory, undoSnapshot.positionHistory, sizeof(positionHistory));
  positionHistoryCount = undoSnapshot.positionHistoryCount;
  humanMoveCount = undoSnapshot.humanMoveCount;
  undoSnapshot.valid = false; // one level only -- used up until the next move
  return true;
}
