// H.G. Muller's micro-Max chess engine for cyd-chess -- declarations only;
// the real documentation, and all of Muller's vendored source, live in
// micromax.cpp.
#ifndef CYD_CHESS_MICROMAX_H
#define CYD_CHESS_MICROMAX_H

void microMaxInit();
void microMaxApplyMove(int fromRow, int fromCol, int toRow, int toCol);
// outScore, if given, receives the root search's own score for the move just
// chosen (positive = good for whichever side just moved) -- see micromax.cpp
// for the negamax sign convention. Used by cyd-chess.ino's move-rating
// feature; every other caller can ignore it.
void microMaxGetBestMove(int &fromRow, int &fromCol, int &toRow, int &toCol, int *outScore = nullptr);

// Speculative "how good would this position be for whoever's about to move"
// search -- runs the real engine search but leaves the actual game board
// untouched (see micromax.cpp for how). Meant to be started the moment it
// becomes the human's turn, so it overlaps their think time instead of
// adding a pause after they move -- see cyd-chess.ino's move-rating feature.
// Safe to call again before a previous call's result has been consumed --
// a no-op in that case (whether still running or already done), since the
// position hasn't changed underneath it.
void microMaxStartBackgroundEval();
// True once a started eval has a result ready to consume.
bool microMaxBackgroundEvalReady();
// Blocks (in case the human moved before the search finished) until a
// started eval is done, then returns its score via outScore and marks it
// consumed. Returns false (outScore left untouched) if no eval was ever
// started -- callers should skip showing a rating in that case rather than
// report a bogus score.
bool microMaxConsumeBackgroundEval(int &outScore);
// Must be called before anything outside this file reads or writes mmB/mmJ/mmZ/mmk/mmR/mmQ/mmO
// directly (today, only undo.cpp's save/restore does) -- otherwise a background eval still
// running against that same state races it. A no-op if nothing is running. microMaxInit(),
// microMaxApplyMove(), and microMaxGetBestMove() already do this themselves; this is only for
// code that reaches into the globals below without going through one of those three.
void microMaxSyncBackgroundEval();

// Exposed only because this sketch's own undo snapshot has to save/restore
// micro-Max's board and running state directly to keep it in lockstep with
// an undone move -- not meant to be poked at from anywhere else. Everything
// else (the hash table, step-vector tables, the search itself) stays
// private to micromax.cpp.
extern signed char mmB[129];
extern int mmJ, mmZ, mmk, mmR, mmQ, mmO;

// How many search nodes the root deepening loop budgets per move (see the
// comment on its definition in micromax.cpp) -- exposed so the .ino's
// difficulty selector can set it before a game starts. Not per-move running
// state like the globals above: microMaxInit() deliberately leaves it alone.
extern int mmNodeBudget;

#endif // CYD_CHESS_MICROMAX_H
