// undo.h
// MIT License
// https://opensource.org/licenses/MIT
// Copyright (c) 2026, Lars Schumann, larsi.org@gmail.com
//
// Single-level undo for cyd-chess -- declarations only; see undo.cpp for
// the real documentation and implementation. Deliberately has no display/
// touch dependency (only chess_rules.h's GameState plus the already-
// extern-exposed running state from micromax.h/book.h), so it's usable
// and testable independent of the ESP32/TFT_eSPI/touch side of the
// sketch, same reasoning as chess_rules.cpp itself.
#ifndef CYD_CHESS_UNDO_H
#define CYD_CHESS_UNDO_H

#include "chess_rules.h" // GameState

// Move-rating's grace-period counter (cyd-chess.ino) -- travels with the snapshot below for the
// same reason plyCount does (see undo.cpp): an undone move should also roll back how many moves
// the human has made, or the rating grace period could end early relative to the reverted game.
extern int humanMoveCount;

void invalidateUndoSnapshot();
void saveUndoSnapshot(GameState &g);
// Returns false (and leaves g untouched) if nothing's been saved yet this
// game -- caller just shows "Nothing to undo" rather than acting.
bool restoreUndoSnapshot(GameState &g);

#endif // CYD_CHESS_UNDO_H
