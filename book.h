// Opening book for cyd-chess -- declarations only; the actual line data and
// lookup logic live in book.cpp. See that file for the real documentation.
#ifndef CYD_CHESS_BOOK_H
#define CYD_CHESS_BOOK_H

#define BOOK_MAX_PLY 6
#define BOOK_NONE -1
struct BookMove { signed char fromRow, fromCol, toRow, toCol; };

// Exposed only because this sketch's own undo snapshot has to save/restore
// them directly to keep the book in lockstep with an undone move -- not
// meant to be poked at from anywhere else. BOOK_LINES itself (the actual
// opening data) has no reason to be visible outside book.cpp and isn't
// declared here.
extern BookMove moveHistory[BOOK_MAX_PLY];
extern int plyCount;
extern bool outOfBook;

void bookReset();
void bookRecordMove(int fromRow, int fromCol, int toRow, int toCol);
bool bookGetMove(int &fromRow, int &fromCol, int &toRow, int &toCol);

#endif // CYD_CHESS_BOOK_H
