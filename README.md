# CYD Chess

A touchscreen chess game for the ESP32-2432S028R "Cheap Yellow Display," started from
[Schematik's guide](https://www.schematik.io/guides/esp32/build-a-touchscreen-chess-game-on-a-cheap-yellow-display)
and substantially rebuilt: a real chess engine, a small opening book, full move and draw rules, a
menu for color/difficulty/new game, pawn promotion, single-level undo, and persistence across a
power cycle.

**Full write-up:** https://larsi.org/make/cyd-chess/

## Hardware & libraries

- ESP32-2432S028R ("Cheap Yellow Display")
- `TFT_eSPI` 2.5.43, `XPT2046_Touchscreen` 1.4, esp32 core 3.3.11
- The board's global `TFT_eSPI/User_Setup.h` must already be configured for this panel (pins
  12/13/14/15/2, HSPI, ILI9341, BGR order, `USE_HSPI_PORT`) -- a sketch-local override hangs
  `tft.init()` instead of working, since `TFT_eSPI.cpp` is a separately-compiled library file
  that only reads the machine-global config.

## Building & flashing

```
arduino-cli compile --fqbn esp32:esp32:esp32 .
arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:esp32 .
```

## File layout

| File | Contents |
|---|---|
| `cyd-chess.ino` | `setup()`/`loop()`, touch handling, board rendering, and the game-lifecycle glue (`resetGame()`, `switchHumanColor()`, `switchAiStrength()`, `completeHumanMove()`/`endGame()`) that ties every other file together. |
| `chess_rules.h`/`.cpp` | The authoritative rules: board/move representation, move generation (castling/en passant/promotion), check/checkmate/stalemate, and draw detection (repetition, 50-move rule, insufficient material). Zero display or touch dependency. |
| `micromax.h`/`.cpp` | [H.G. Muller's micro-Max 4.8](https://home.hccnet.nl/h.g.muller/umax4_8.c) engine, vendored near-verbatim. Used only to propose move candidates -- every move it suggests is cross-checked against `chess_rules.cpp`'s own legal-move list before being trusted. |
| `book.h`/`.cpp` | A 14-line opening book (from [lichess-org/chess-openings](https://github.com/lichess-org/chess-openings), CC0), checked before the engine searches. |
| `pieces.h` | Three selectable piece bitmap sets (MENU's PIECE SET option) -- Classic, converted from the "SmallPng" set in [samboy/ChessGraphics](https://github.com/samboy/ChessGraphics) (public domain); Pixel and Playful, both cropped from Brianna Cason's ("hanahoa") ["Chess and Checkers"](https://hanahoa.itch.io/chess-and-checkers) itch.io asset (CC BY 4.0) -- just the bitmap tables, no drawing logic. |
| `menu.h`/`.cpp` | The MENU/difficulty/promotion screens: drawing and touch-hit-testing only. |
| `undo.h`/`.cpp` | The single-level undo snapshot. Zero display dependency. |
| `persistence.h`/`.cpp` | Saves/resumes the whole game, and separately the color/difficulty settings, across a power cycle via the ESP32 `Preferences`/NVS library. Zero display dependency. |

`chess_rules`/`micromax`/`book`/`undo`/`persistence` have no display or touch dependency at all,
so they're usable and testable independent of the ESP32 side of the sketch. Development leaned on
a set of off-device `g++` test harnesses (self-play against the real files, opening-line replay,
undo/persistence round-trip exactness, draw-condition edge cases) that link these files directly,
stubbing only `Arduino.h`/`Preferences.h` -- not checked into this repo, but every change described
in the full write-up above was verified that way before ever touching real hardware.

See the [full write-up](https://larsi.org/make/cyd-chess/) for how each piece works and why.
