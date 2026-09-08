// Persists (or resumes) a full in-progress cyd-chess game -- board,
// engine/book running state, and draw-detection history, plus which color
// the human plays and at what difficulty -- to the ESP32's NVS flash via
// the Preferences library, so a power cycle mid-game can resume exactly
// where it left off. See persistence.cpp for what's actually stored and
// why; declarations only here.
#ifndef CYD_CHESS_PERSISTENCE_H
#define CYD_CHESS_PERSISTENCE_H

#include "chess_rules.h" // GameState

void savePersistedGame(GameState &gs, int humanColor, int aiStrength);
// Returns false (and leaves gs/humanColor/aiStrength untouched) if there's
// no saved game to resume -- caller falls back to its own fresh-game init.
bool loadPersistedGame(GameState &gs, int &humanColor, int &aiStrength);
void clearPersistedGame();

#endif // CYD_CHESS_PERSISTENCE_H
