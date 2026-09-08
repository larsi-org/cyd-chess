// Whole-game persistence for cyd-chess -- see persistence.h for the public
// interface. Deliberately its own module: needs zero display/touch
// dependency (only the already-extern-exposed state from chess_rules.h/
// micromax.h/book.h), so it's usable and testable independent of the
// ESP32/TFT_eSPI/touch side of the sketch, same reasoning as
// chess_rules.cpp itself.
#include <Arduino.h> // memcpy
#include <Preferences.h>
#include "persistence.h"
#include "micromax.h"
#include "book.h"

// Everything needed to fully resume a game -- deliberately the same field
// list UndoSnapshot (in cyd-chess.ino) already uses, plus humanColor/
// aiStrength, so a resumed game comes back configured the same way, not
// just the same board. A *separate* struct from UndoSnapshot rather than a
// reuse of it, though: this is captured at a different moment (right after
// a move completes, representing the current position) than Undo's own
// snapshot (right before the human's move, specifically so Undo can revert
// to it) -- same shape, different point in the move sequence.
struct SavedGame {
  GameState gs;
  signed char mmB[129];
  int mmJ, mmZ, mmk, mmR, mmQ, mmO;
  int plyCount;
  bool outOfBook;
  BookMove moveHistory[BOOK_MAX_PLY];
  uint32_t positionHistory[MAX_POSITION_HISTORY];
  int positionHistoryCount;
  int humanColor;
  int aiStrength;
};

#define PREFS_NAMESPACE "cydchess"
#define PREFS_KEY       "save"

void savePersistedGame(GameState &gs, int humanColor, int aiStrength) {
  SavedGame saved;
  saved.gs = gs;
  memcpy(saved.mmB, mmB, sizeof(mmB));
  saved.mmJ = mmJ; saved.mmZ = mmZ; saved.mmk = mmk;
  saved.mmR = mmR; saved.mmQ = mmQ; saved.mmO = mmO;
  saved.plyCount = plyCount;
  saved.outOfBook = outOfBook;
  memcpy(saved.moveHistory, moveHistory, sizeof(moveHistory));
  memcpy(saved.positionHistory, positionHistory, sizeof(positionHistory));
  saved.positionHistoryCount = positionHistoryCount;
  saved.humanColor = humanColor;
  saved.aiStrength = aiStrength;

  Preferences prefs;
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.putBytes(PREFS_KEY, &saved, sizeof(saved));
  prefs.end();
}

bool loadPersistedGame(GameState &gs, int &humanColor, int &aiStrength) {
  Preferences prefs;
  prefs.begin(PREFS_NAMESPACE, false);

  // Only trust a blob whose length exactly matches -- a mismatched size
  // (nothing saved yet, or a stale layout from a firmware update that
  // changed SavedGame's shape) means there's nothing safe to resume from.
  if (prefs.getBytesLength(PREFS_KEY) != sizeof(SavedGame)) {
    prefs.end();
    return false;
  }

  SavedGame saved;
  prefs.getBytes(PREFS_KEY, &saved, sizeof(saved));
  prefs.end();

  gs = saved.gs;
  memcpy(mmB, saved.mmB, sizeof(mmB));
  mmJ = saved.mmJ; mmZ = saved.mmZ; mmk = saved.mmk;
  mmR = saved.mmR; mmQ = saved.mmQ; mmO = saved.mmO;
  plyCount = saved.plyCount;
  outOfBook = saved.outOfBook;
  memcpy(moveHistory, saved.moveHistory, sizeof(moveHistory));
  memcpy(positionHistory, saved.positionHistory, sizeof(positionHistory));
  positionHistoryCount = saved.positionHistoryCount;
  humanColor = saved.humanColor;
  aiStrength = saved.aiStrength;
  return true;
}

void clearPersistedGame() {
  Preferences prefs;
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.remove(PREFS_KEY);
  prefs.end();
}
