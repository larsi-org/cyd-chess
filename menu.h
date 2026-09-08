// Menu/difficulty/promotion screens for cyd-chess -- the standalone-
// takeover UI screens and their touch-hit-testing, split out of
// cyd-chess.ino once it grew past ~1000 lines. Needs tft/touch (declared
// extern in menu.cpp, defined in cyd-chess.ino) so isn't "pure" the way
// chess_rules/micromax/book/undo are, but it's still a self-contained
// concept: given constants and touch input, draw things and report hits.
// Deliberately does NOT include resetGame()/switchHumanColor()/
// switchAiStrength() -- those reset engine/book/undo/persisted-game state
// too, so they're game-lifecycle glue that stays in cyd-chess.ino next to
// setup()/loop()/completeHumanMove(), not menu code.
#ifndef CYD_CHESS_MENU_H
#define CYD_CHESS_MENU_H

void drawMenuButton();
void drawUndoButton();
bool isTouchOnMenuButton();
bool isTouchOnUndoButton();

void drawMenuScreen(int humanColor, int aiStrength);
bool isTouchOnMenuNewGameButton();
bool isTouchOnPlayWhiteButton();
bool isTouchOnPlayBlackButton();
bool isTouchOnMenuDifficultyButton();

void drawDifficultyMenu();
bool isTouchOnDifficultyButton(int idx);

void drawPromotionMenu();
bool isTouchOnPromotionButton(int idx);
// Maps a promotion-menu button index (0..3) to the piece it commits to --
// shared with cyd-chess.ino's loop(), which sets Move::promoteTo from it
// once a choice is tapped.
extern const int PROMO_PIECES[4];

#endif // CYD_CHESS_MENU_H
