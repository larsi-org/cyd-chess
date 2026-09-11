// persistence.cpp
// MIT License
// https://opensource.org/licenses/MIT
// Copyright (c) 2026, Lars Schumann, larsi.org@gmail.com
//
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

// Everything needed to fully resume a game -- the same field list
// UndoSnapshot (in cyd-chess.ino) already uses, minus humanColor/
// aiStrength/pieceSet: those live in their own tiny SavedSettings record
// instead (see persistence.h's own comment for why), so this struct only
// has to change when the board/engine/book/history actually does --
// which, in practice, is exactly "a real move happened," never "someone
// tapped a color, difficulty, or piece set button."
struct SavedGame {
  GameState gs;
  signed char mmB[129];
  int mmJ, mmZ, mmk, mmR, mmQ, mmO;
  int plyCount;
  bool outOfBook;
  BookMove moveHistory[BOOK_MAX_PLY];
  uint32_t positionHistory[MAX_POSITION_HISTORY];
  int positionHistoryCount;
};

struct SavedSettings {
  int humanColor;
  int aiStrength;
  int pieceSet;
};

#define PREFS_NAMESPACE     "cydchess"
#define PREFS_KEY_GAME      "save"
#define PREFS_KEY_SETTINGS  "settings"

// Writes `value` to `key` unless an identical value is already stored
// there. The read is free in wear terms (flash reads don't degrade the
// cells at all, only erase/program cycles do); skipping the write when
// nothing changed avoids a real flash write for a genuine no-op (e.g.
// tapping PLAY WHITE while already White). This is as fine-grained as
// skipping-unchanged-writes can usefully get here: flash only erases in
// whole sectors, never per-byte, and Preferences' own putBytes() always
// replaces a key's stored value wholesale rather than patching it in
// place -- so there's no partial-update primitive underneath for a
// byte-level diff to hand anything to, even if we computed one ourselves.
template <typename T>
static void putIfChanged(Preferences &prefs, const char *key, const T &value) {
  T existing;
  bool unchanged = prefs.getBytesLength(key) == sizeof(T) &&
                    prefs.getBytes(key, &existing, sizeof(T)) == sizeof(T) &&
                    memcmp(&existing, &value, sizeof(T)) == 0;
  if (!unchanged) prefs.putBytes(key, &value, sizeof(T));
}

void saveSettings(int humanColor, int aiStrength, int pieceSet) {
  SavedSettings s = {humanColor, aiStrength, pieceSet};
  Preferences prefs;
  prefs.begin(PREFS_NAMESPACE, false);
  putIfChanged(prefs, PREFS_KEY_SETTINGS, s);
  prefs.end();
}

bool loadSettings(int &humanColor, int &aiStrength, int &pieceSet) {
  Preferences prefs;
  prefs.begin(PREFS_NAMESPACE, false);
  SavedSettings s;
  bool ok = prefs.getBytesLength(PREFS_KEY_SETTINGS) == sizeof(s) &&
            prefs.getBytes(PREFS_KEY_SETTINGS, &s, sizeof(s)) == sizeof(s);
  prefs.end();
  if (!ok) return false;
  humanColor = s.humanColor;
  aiStrength = s.aiStrength;
  pieceSet = s.pieceSet;
  return true;
}

void savePersistedGame(GameState &gs) {
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

  Preferences prefs;
  prefs.begin(PREFS_NAMESPACE, false);
  putIfChanged(prefs, PREFS_KEY_GAME, saved);
  prefs.end();
}

bool loadPersistedGame(GameState &gs) {
  Preferences prefs;
  prefs.begin(PREFS_NAMESPACE, false);

  // Only trust a blob whose length exactly matches -- a mismatched size
  // (nothing saved yet, or a stale layout from a firmware update that
  // changed SavedGame's shape) means there's nothing safe to resume from.
  if (prefs.getBytesLength(PREFS_KEY_GAME) != sizeof(SavedGame)) {
    prefs.end();
    return false;
  }

  SavedGame saved;
  prefs.getBytes(PREFS_KEY_GAME, &saved, sizeof(saved));
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
  return true;
}

void clearPersistedGame() {
  Preferences prefs;
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.remove(PREFS_KEY_GAME);
  prefs.end();
}
