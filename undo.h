// Single-level undo for cyd-chess -- declarations only; see undo.cpp for
// the real documentation and implementation. Deliberately has no display/
// touch dependency (only chess_rules.h's GameState plus the already-
// extern-exposed running state from micromax.h/book.h), so it's usable
// and testable independent of the ESP32/TFT_eSPI/touch side of the
// sketch, same reasoning as chess_rules.cpp itself.
#ifndef CYD_CHESS_UNDO_H
#define CYD_CHESS_UNDO_H

#include "chess_rules.h" // GameState

void invalidateUndoSnapshot();
void saveUndoSnapshot(GameState &g);
// Returns false (and leaves g untouched) if nothing's been saved yet this
// game -- caller just shows "Nothing to undo" rather than acting.
bool restoreUndoSnapshot(GameState &g);

#endif // CYD_CHESS_UNDO_H
