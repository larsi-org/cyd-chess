// Menu/difficulty/promotion screens for cyd-chess -- see menu.h for the
// public interface.
#include <Arduino.h> // strlen
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include "menu.h"
#include "chess_rules.h" // QUEEN/ROOK/BISHOP/KNIGHT (PROMO_PIECES, below)
#include "pieces.h" // NUM_PIECE_SETS/PIECE_SET_NAMES

// Defined in cyd-chess.ino -- the hardware objects every screen here draws
// to and reads touches from.
extern TFT_eSPI tft;
extern XPT2046_Touchscreen touch;

// Duplicated from cyd-chess.ino rather than shared through a header --
// small, stable values (touch panel calibration; a couple of board/piece
// colors also used by drawPiece()/drawSquare() over there), not worth a
// dedicated shared-constants header just to avoid repeating four #defines.
#define TOUCH_X_MIN 200
#define TOUCH_X_MAX 3800
#define TOUCH_Y_MIN 300
#define TOUCH_Y_MAX 3700
#define COLOR_BG      0x0000
#define COLOR_WHITE_P 0xFFFF
#define COLOR_BLACK_P 0x18C3

void drawTextButton(int x, int y, int w, int h, const char *label,
                     uint16_t fillColor, uint16_t borderColor, uint16_t textColor = TFT_WHITE) {
  tft.fillRoundRect(x, y, w, h, 4, fillColor);
  tft.drawRoundRect(x, y, w, h, 4, borderColor);
  tft.setTextSize(1);
  tft.setTextColor(textColor, fillColor);
  // Centred-ish text — TFT_eSPI default font is ~6 pixels per character.
  tft.setCursor(x + (w - (int)strlen(label) * 6) / 2, y + (h - 8) / 2);
  tft.print(label);
}

bool isTouchInButton(int x, int y, int w, int h) {
  if (!touch.touched()) return false;
  TS_Point p = touch.getPoint();
  int tx = map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, 240);
  int ty = map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, 320);
  return (tx >= x && tx < x + w && ty >= y && ty < y + h);
}

// Undo / Menu buttons — bottom of screen, below the chess board.
// 240×320 portrait: board occupies y=0..240; column labels at y=242..250;
// status line at y=260..276; buttons get the remaining strip y=294..318,
// split into two side by side (5px margins, 10px gap: 5+110+10+110+5 = 240).
#define UNDO_BTN_X        5
#define MENU_BOTTOM_BTN_X 125
#define BTN_Y             294
#define BTN_W             110
#define BTN_H             24

// Was "NEW GAME" -- now opens the color-choice menu instead of resetting
// directly, same button slot/color.
void drawMenuButton() {
  drawTextButton(MENU_BOTTOM_BTN_X, BTN_Y, BTN_W, BTN_H, "MENU", TFT_DARKGREEN, TFT_GREEN);
}

// Amber rather than Menu's green, so the two are easy to tell apart at
// a glance -- a common "undo" color and distinct from "start over".
void drawUndoButton() {
  drawTextButton(UNDO_BTN_X, BTN_Y, BTN_W, BTN_H, "UNDO", 0x8400 /* dark amber */, TFT_ORANGE);
}

bool isTouchOnMenuButton() {
  return isTouchInButton(MENU_BOTTOM_BTN_X, BTN_Y, BTN_W, BTN_H);
}

bool isTouchOnUndoButton() {
  return isTouchInButton(UNDO_BTN_X, BTN_Y, BTN_W, BTN_H);
}

// ─── Menu screen ────────────────────────────────────────────────────────────
// Covers the whole screen (board/buttons hidden) while inMenu is true. Four
// independent live settings live here (PLAY WHITE/PLAY BLACK, DIFFICULTY,
// PIECE SET -- each applies immediately, mid-game, no reset, via
// switchHumanColor()/switchAiStrength()/switchPieceSet() in cyd-chess.ino)
// plus NEW GAME, which just starts a fresh game using whatever color/
// difficulty/piece set are *currently* set rather than asking again -- each
// is a standalone, always-live choice, not tied to starting a new game.
// Button height/gap tightened (was 45/8, for 4 buttons) to fit 5 in the
// 320px screen -- same squeeze PIECE SET's own addition needed here as
// DIFFICULTY's did the first time.
#define MENU_BTN_W           200
#define MENU_BTN_H           38
#define MENU_BTN_GAP         6
#define MENU_BTN_X           ((240 - MENU_BTN_W) / 2)
#define MENU_NEWGAME_BTN_Y   82
#define MENU_WHITE_BTN_Y     (MENU_NEWGAME_BTN_Y + MENU_BTN_H + MENU_BTN_GAP)
#define MENU_BLACK_BTN_Y     (MENU_WHITE_BTN_Y + MENU_BTN_H + MENU_BTN_GAP)
#define MENU_DIFF_BTN_Y      (MENU_BLACK_BTN_Y + MENU_BTN_H + MENU_BTN_GAP)
#define MENU_PIECESET_BTN_Y  (MENU_DIFF_BTN_Y + MENU_BTN_H + MENU_BTN_GAP)

const char *STRENGTH_NAMES[4] = {"EASY", "MEDIUM", "HARD", "EXPERT"};

void drawMenuScreen(int humanColor, int aiStrength, int pieceSet) {
  tft.fillScreen(COLOR_BG);
  tft.setTextSize(2);
  tft.setTextColor(TFT_YELLOW, COLOR_BG);
  tft.setCursor(60, 28);
  tft.print("Menu");

  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, COLOR_BG);
  char caption[40];
  snprintf(caption, sizeof(caption), "Now playing: %s, %s",
           humanColor == WHITE_PIECE ? "White" : "Black", STRENGTH_NAMES[aiStrength]);
  tft.setCursor((240 - (int)strlen(caption) * 6) / 2, 58);
  tft.print(caption);

  drawTextButton(MENU_BTN_X, MENU_NEWGAME_BTN_Y, MENU_BTN_W, MENU_BTN_H,
                 "NEW GAME", TFT_DARKGREEN, TFT_GREEN);
  drawTextButton(MENU_BTN_X, MENU_WHITE_BTN_Y, MENU_BTN_W, MENU_BTN_H,
                 "PLAY WHITE", COLOR_WHITE_P, TFT_BLACK, TFT_BLACK);
  drawTextButton(MENU_BTN_X, MENU_BLACK_BTN_Y, MENU_BTN_W, MENU_BTN_H,
                 "PLAY BLACK", COLOR_BLACK_P, TFT_WHITE, TFT_WHITE);
  drawTextButton(MENU_BTN_X, MENU_DIFF_BTN_Y, MENU_BTN_W, MENU_BTN_H,
                 "DIFFICULTY", TFT_NAVY, TFT_CYAN, TFT_WHITE);
  char pieceSetLabel[24];
  snprintf(pieceSetLabel, sizeof(pieceSetLabel), "PIECES: %s", PIECE_SET_NAMES[pieceSet]);
  drawTextButton(MENU_BTN_X, MENU_PIECESET_BTN_Y, MENU_BTN_W, MENU_BTN_H,
                 pieceSetLabel, TFT_PURPLE, TFT_MAGENTA, TFT_WHITE);
}

bool isTouchOnMenuNewGameButton() {
  return isTouchInButton(MENU_BTN_X, MENU_NEWGAME_BTN_Y, MENU_BTN_W, MENU_BTN_H);
}

bool isTouchOnPlayWhiteButton() {
  return isTouchInButton(MENU_BTN_X, MENU_WHITE_BTN_Y, MENU_BTN_W, MENU_BTN_H);
}

bool isTouchOnPlayBlackButton() {
  return isTouchInButton(MENU_BTN_X, MENU_BLACK_BTN_Y, MENU_BTN_W, MENU_BTN_H);
}

bool isTouchOnMenuDifficultyButton() {
  return isTouchInButton(MENU_BTN_X, MENU_DIFF_BTN_Y, MENU_BTN_W, MENU_BTN_H);
}

bool isTouchOnMenuPieceSetButton() {
  return isTouchInButton(MENU_BTN_X, MENU_PIECESET_BTN_Y, MENU_BTN_W, MENU_BTN_H);
}

// ─── Difficulty screen (reached via MENU's DIFFICULTY button) ─────────────
// Picking a level here applies immediately (switchAiStrength() in
// cyd-chess.ino, same live-setting treatment as PLAY WHITE/PLAY BLACK) --
// it does not start a new game.
#define DIFF_BTN_W    200
#define DIFF_BTN_H    45
#define DIFF_BTN_X    ((240 - DIFF_BTN_W) / 2)
#define DIFF_BTN_GAP  10
#define DIFF_BTN_Y0   90
#define DIFF_BTN_Y(i) (DIFF_BTN_Y0 + (i) * (DIFF_BTN_H + DIFF_BTN_GAP))

void drawDifficultyMenu() {
  tft.fillScreen(COLOR_BG);
  tft.setTextSize(2);
  tft.setTextColor(TFT_YELLOW, COLOR_BG);
  tft.setCursor(28, 50);
  tft.print("Difficulty");

  // Light-to-dark progression (green..red), text color kept readable on each.
  uint16_t fill[4]    = {TFT_GREEN, TFT_YELLOW, TFT_ORANGE, TFT_RED};
  uint16_t border[4]  = {TFT_BLACK, TFT_BLACK,  TFT_BLACK,  TFT_WHITE};
  uint16_t textCol[4] = {TFT_BLACK, TFT_BLACK,  TFT_BLACK,  TFT_WHITE};

  for (int i = 0; i < 4; i++) {
    drawTextButton(DIFF_BTN_X, DIFF_BTN_Y(i), DIFF_BTN_W, DIFF_BTN_H,
                   STRENGTH_NAMES[i], fill[i], border[i], textCol[i]);
  }
}

bool isTouchOnDifficultyButton(int idx) {
  return isTouchInButton(DIFF_BTN_X, DIFF_BTN_Y(idx), DIFF_BTN_W, DIFF_BTN_H);
}

// ─── Piece set screen (reached via MENU's PIECE SET button) ───────────────
// Picking a set here applies immediately (switchPieceSet() in
// cyd-chess.ino, same live-setting treatment as DIFFICULTY) -- it does not
// start a new game. Named button per NUM_PIECE_SETS/PIECE_SET_NAMES
// (pieces.h), so a third set later needs no layout change here.
#define PIECESET_BTN_W    200
#define PIECESET_BTN_H    45
#define PIECESET_BTN_X    ((240 - PIECESET_BTN_W) / 2)
#define PIECESET_BTN_GAP  10
#define PIECESET_BTN_Y0   90
#define PIECESET_BTN_Y(i) (PIECESET_BTN_Y0 + (i) * (PIECESET_BTN_H + PIECESET_BTN_GAP))

void drawPieceSetMenu() {
  tft.fillScreen(COLOR_BG);
  tft.setTextSize(2);
  tft.setTextColor(TFT_YELLOW, COLOR_BG);
  tft.setCursor(30, 50);
  tft.print("Piece Set");

  for (int i = 0; i < NUM_PIECE_SETS; i++) {
    drawTextButton(PIECESET_BTN_X, PIECESET_BTN_Y(i), PIECESET_BTN_W, PIECESET_BTN_H,
                   PIECE_SET_NAMES[i], TFT_PURPLE, TFT_MAGENTA, TFT_WHITE);
  }
}

bool isTouchOnPieceSetButton(int idx) {
  return isTouchInButton(PIECESET_BTN_X, PIECESET_BTN_Y(idx), PIECESET_BTN_W, PIECESET_BTN_H);
}

// ─── Promotion choice ───────────────────────────────────────────────────────
// Shown only for the human's own promoting moves -- AI/book moves always
// auto-queen (see promoteTo's own comment in chess_rules.h; correct almost
// always, and consistent with how micro-Max evaluates a promoted pawn
// internally regardless of what this sketch's own board says it became).
#define PROMO_BTN_W    200
#define PROMO_BTN_H    45
#define PROMO_BTN_X    ((240 - PROMO_BTN_W) / 2)
#define PROMO_BTN_GAP  10
#define PROMO_BTN_Y0   80
#define PROMO_BTN_Y(i) (PROMO_BTN_Y0 + (i) * (PROMO_BTN_H + PROMO_BTN_GAP))

const char *PROMO_NAMES[4] = {"QUEEN", "ROOK", "BISHOP", "KNIGHT"};
const int PROMO_PIECES[4]  = {QUEEN, ROOK, BISHOP, KNIGHT};

void drawPromotionMenu() {
  tft.fillScreen(COLOR_BG);
  tft.setTextSize(2);
  tft.setTextColor(TFT_YELLOW, COLOR_BG);
  tft.setCursor(20, 40);
  tft.print("Promote to:");
  for (int i = 0; i < 4; i++) {
    drawTextButton(PROMO_BTN_X, PROMO_BTN_Y(i), PROMO_BTN_W, PROMO_BTN_H,
                   PROMO_NAMES[i], TFT_NAVY, TFT_CYAN, TFT_WHITE);
  }
}

bool isTouchOnPromotionButton(int idx) {
  return isTouchInButton(PROMO_BTN_X, PROMO_BTN_Y(idx), PROMO_BTN_W, PROMO_BTN_H);
}
