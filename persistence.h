// Persists (or resumes) a full in-progress cyd-chess game -- board,
// engine/book running state, and draw-detection history -- to the ESP32's
// NVS flash via the Preferences library, so a power cycle mid-game can
// resume exactly where it left off. Kept as a separate, tiny "settings"
// record (humanColor/aiStrength/pieceSet) from the big game blob
// deliberately: a color/difficulty/piece-set change writes only the
// few-byte settings record, cheap enough to do unconditionally on every
// change, while the ~3.1KB game blob only ever gets written when there's
// real progress to protect (see the call sites in cyd-chess.ino). See
// persistence.cpp for the rest of the reasoning; declarations only here.
#ifndef CYD_CHESS_PERSISTENCE_H
#define CYD_CHESS_PERSISTENCE_H

#include "chess_rules.h" // GameState

void saveSettings(int humanColor, int aiStrength, int pieceSet);
// Returns false (and leaves humanColor/aiStrength/pieceSet untouched) if
// nothing's ever been saved -- caller keeps its own compiled-in defaults.
bool loadSettings(int &humanColor, int &aiStrength, int &pieceSet);

void savePersistedGame(GameState &gs);
// Returns false (and leaves gs untouched) if there's no saved game to
// resume -- caller falls back to its own fresh-game init.
bool loadPersistedGame(GameState &gs);
void clearPersistedGame();

#endif // CYD_CHESS_PERSISTENCE_H
