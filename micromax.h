// H.G. Muller's micro-Max chess engine for cyd-chess -- declarations only;
// the real documentation, and all of Muller's vendored source, live in
// micromax.cpp.
#ifndef CYD_CHESS_MICROMAX_H
#define CYD_CHESS_MICROMAX_H

void microMaxInit();
void microMaxApplyMove(int fromRow, int fromCol, int toRow, int toCol);
void microMaxGetBestMove(int &fromRow, int &fromCol, int &toRow, int &toCol);

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
