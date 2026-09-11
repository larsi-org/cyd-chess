// menu.cpp
// MIT License
// https://opensource.org/licenses/MIT
// Copyright (c) 2026, Lars Schumann, larsi.org@gmail.com
//
// Menu/difficulty/promotion screens for cyd-chess -- see menu.h for the
// public interface.
#include <Arduino.h> // strlen
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include "menu.h"
#include "chess_rules.h" // QUEEN/ROOK/BISHOP/KNIGHT (PROMO_PIECES, below)
#include "color565.h"
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
// Values here must stay in sync with cyd-chess.ino's constants of the same name -- both now come
// from color565.h's shared web-safe palette (larsi.org/graphics/colors/).
constexpr uint16_t COLOR_BG      = COLOR565_BLACK;
constexpr uint16_t COLOR_WHITE_P = COLOR565_WHITE;
constexpr uint16_t COLOR_BLACK_P = COLOR565_BLACK;

void drawTextButton(int x, int y, int w, int h, const char *label,
                     uint16_t fillColor, uint16_t borderColor, uint16_t textColor = COLOR565_WHITE) {
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
  drawTextButton(MENU_BOTTOM_BTN_X, BTN_Y, BTN_W, BTN_H, "MENU", COLOR565_DARK_GREEN, COLOR565_LIME);
}

// Amber rather than Menu's green, so the two are easy to tell apart at
// a glance -- a common "undo" color and distinct from "start over".
void drawUndoButton() {
  drawTextButton(UNDO_BTN_X, BTN_Y, BTN_W, BTN_H, "UNDO", COLOR565_GOLD4, COLOR565_ORANGE);
}

bool isTouchOnMenuButton() {
  return isTouchInButton(MENU_BOTTOM_BTN_X, BTN_Y, BTN_W, BTN_H);
}

bool isTouchOnUndoButton() {
  return isTouchInButton(UNDO_BTN_X, BTN_Y, BTN_W, BTN_H);
}

// ─── Menu screen ────────────────────────────────────────────────────────────
// Covers the whole screen (board/buttons hidden) while inMenu is true. Three
// independent live settings live here (COLOR, DIFFICULTY, PIECE SET -- each
// applies immediately, mid-game, no reset, via switchHumanColor()/
// switchAiStrength()/switchPieceSet() in cyd-chess.ino) plus NEW GAME, which
// just starts a fresh game using whatever color/difficulty/piece set are
// *currently* set rather than asking again -- each is a standalone,
// always-live choice, not tied to starting a new game. Every button prints
// its own current value (was a separate "Now playing: ..." caption above the
// buttons before COLOR became one button instead of two) so there's exactly
// one place to look for each setting.
#define MENU_BTN_W           200
#define MENU_BTN_H           45
#define MENU_BTN_GAP         8
#define MENU_BTN_X           ((240 - MENU_BTN_W) / 2)
#define MENU_NEWGAME_BTN_Y   60
#define MENU_COLOR_BTN_Y     (MENU_NEWGAME_BTN_Y + MENU_BTN_H + MENU_BTN_GAP)
#define MENU_DIFF_BTN_Y      (MENU_COLOR_BTN_Y + MENU_BTN_H + MENU_BTN_GAP)
#define MENU_PIECESET_BTN_Y  (MENU_DIFF_BTN_Y + MENU_BTN_H + MENU_BTN_GAP)

const char *STRENGTH_NAMES[4] = {"EASY", "MEDIUM", "HARD", "EXPERT"};

void drawMenuScreen(int humanColor, int aiStrength, int pieceSet) {
  tft.fillScreen(COLOR_BG);
  tft.setTextSize(2);
  tft.setTextColor(COLOR565_YELLOW, COLOR_BG);
  tft.setCursor(60, 28);
  tft.print("Menu");

  drawTextButton(MENU_BTN_X, MENU_NEWGAME_BTN_Y, MENU_BTN_W, MENU_BTN_H,
                 "NEW GAME", COLOR565_DARK_GREEN, COLOR565_LIME);

  // A toggle, not a submenu -- only two values, so tapping it flips straight
  // to the other color (cyd-chess.ino's loop()) rather than opening a
  // separate picker screen the way DIFFICULTY/PIECE SET do. Button colors
  // themselves flip with the value (white-on-black text on a white button
  // when playing White, the reverse when playing Black), the same visual
  // identity the old separate PLAY WHITE/PLAY BLACK buttons each had.
  char colorLabel[16];
  snprintf(colorLabel, sizeof(colorLabel), "PLAYER: %s", humanColor == WHITE_PIECE ? "WHITE" : "BLACK");
  uint16_t colorFill = (humanColor == WHITE_PIECE) ? COLOR_WHITE_P : COLOR_BLACK_P;
  uint16_t colorInk  = (humanColor == WHITE_PIECE) ? COLOR565_BLACK : COLOR565_WHITE;
  drawTextButton(MENU_BTN_X, MENU_COLOR_BTN_Y, MENU_BTN_W, MENU_BTN_H,
                 colorLabel, colorFill, colorInk, colorInk);

  char diffLabel[24];
  snprintf(diffLabel, sizeof(diffLabel), "DIFFICULTY: %s", STRENGTH_NAMES[aiStrength]);
  drawTextButton(MENU_BTN_X, MENU_DIFF_BTN_Y, MENU_BTN_W, MENU_BTN_H,
                 diffLabel, COLOR565_DARK_BLUE, COLOR565_CYAN, COLOR565_WHITE);
  char pieceSetLabel[24];
  snprintf(pieceSetLabel, sizeof(pieceSetLabel), "PIECES: %s", PIECE_SET_NAMES[pieceSet]);
  drawTextButton(MENU_BTN_X, MENU_PIECESET_BTN_Y, MENU_BTN_W, MENU_BTN_H,
                 pieceSetLabel, COLOR565_PURPLE, COLOR565_MAGENTA, COLOR565_WHITE);
}

bool isTouchOnMenuNewGameButton() {
  return isTouchInButton(MENU_BTN_X, MENU_NEWGAME_BTN_Y, MENU_BTN_W, MENU_BTN_H);
}

bool isTouchOnMenuColorButton() {
  return isTouchInButton(MENU_BTN_X, MENU_COLOR_BTN_Y, MENU_BTN_W, MENU_BTN_H);
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
  tft.setTextColor(COLOR565_YELLOW, COLOR_BG);
  tft.setCursor(28, 50);
  tft.print("Difficulty");

  // Light-to-dark progression (green..red), text color kept readable on each.
  uint16_t fill[4]    = {COLOR565_LIME, COLOR565_YELLOW, COLOR565_ORANGE, COLOR565_RED};
  uint16_t border[4]  = {COLOR565_BLACK, COLOR565_BLACK,  COLOR565_BLACK,  COLOR565_WHITE};
  uint16_t textCol[4] = {COLOR565_BLACK, COLOR565_BLACK,  COLOR565_BLACK,  COLOR565_WHITE};

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
  tft.setTextColor(COLOR565_YELLOW, COLOR_BG);
  tft.setCursor(30, 50);
  tft.print("Piece Set");

  for (int i = 0; i < NUM_PIECE_SETS; i++) {
    drawTextButton(PIECESET_BTN_X, PIECESET_BTN_Y(i), PIECESET_BTN_W, PIECESET_BTN_H,
                   PIECE_SET_NAMES[i], COLOR565_PURPLE, COLOR565_MAGENTA, COLOR565_WHITE);
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
  tft.setTextColor(COLOR565_YELLOW, COLOR_BG);
  tft.setCursor(20, 40);
  tft.print("Promote to:");
  for (int i = 0; i < 4; i++) {
    drawTextButton(PROMO_BTN_X, PROMO_BTN_Y(i), PROMO_BTN_W, PROMO_BTN_H,
                   PROMO_NAMES[i], COLOR565_DARK_BLUE, COLOR565_CYAN, COLOR565_WHITE);
  }
}

bool isTouchOnPromotionButton(int idx) {
  return isTouchInButton(PROMO_BTN_X, PROMO_BTN_Y(idx), PROMO_BTN_W, PROMO_BTN_H);
}
