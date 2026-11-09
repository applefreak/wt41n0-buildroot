/* Copied unchanged from claude-design/tokens.h (the launcher UI spec, claude-design/SPEC.md).
 * Edit the spec and copy it again rather than changing values here. */
/* WT41N0 launcher — design tokens and layout constants.
 * Generated from the UI spec (SPEC.md). All sizes in px on the 320x240 grid.
 * Colours are RGB888; convert with lv_color_hex(). Alpha tokens are LV_OPA values
 * to blend with lv_color_mix() against the surface they sit on, or pre-blend at build time.
 */
#ifndef WT_TOKENS_H
#define WT_TOKENS_H

#include <stdint.h>

/* ---- screen and fixed frame ---- */
#define WT_SCREEN_W        320
#define WT_SCREEN_H        240
#define WT_STATUS_Y          0
#define WT_STATUS_H         20
#define WT_CONTENT_Y        20
#define WT_CONTENT_H       204
#define WT_HINT_Y          224
#define WT_HINT_H           16
#define WT_BAR_PAD_X         8   /* status and hint bar side padding */
#define WT_BAR_GAP           8   /* status bar item gap */
#define WT_HINT_GAP         12   /* hint pairs gap; key-label gap is 5 */

/* content proper (inside the 4 px decor band) */
#define WT_INNER_X           8
#define WT_INNER_Y          26
#define WT_INNER_W         304
#define WT_INNER_H         192
#define WT_SCROLL_X        308   /* scroll indicator column */
#define WT_SCROLL_W          5
#define WT_ROW_W           296   /* row width when a scroll indicator is present */

/* tiles (home) */
#define WT_TILE_W           98
#define WT_TILE_H           72
#define WT_TILE_GAP          5
#define WT_TILE_X0           8   /* columns at 8, 111, 214 */
#define WT_TILE_Y0          26   /* rows at 26, 103 */
#define WT_READOUT_Y       180
#define WT_READOUT_H        36

/* lists */
#define WT_ROW_H            36
#define WT_ROW_GAP           2   /* rows at 26 + 38n, five visible */
#define WT_ROW_COMPACT_H    34   /* under a 40 px card: rows at 70 + 36n */
#define WT_CARD_H           40

/* buttons, fields */
#define WT_BTN_WIDE_W      149   /* CONNECT / FORGET at x 8 and 163, y 148, h 36 */
#define WT_BTN_WIDE_H       36
#define WT_BTN_BIG_H        72   /* REBOOT / POWER OFF at y 48 and 128 */
#define WT_FIELD_Y          46
#define WT_FIELD_H          32
#define WT_TOGGLE_ROW_H     28   /* at y 84 and 114 */
#define WT_STATUS_LINE_Y   192
#define WT_STATUS_LINE_H    24

/* slider card */
#define WT_SLIDER_CARD_H   190
#define WT_SLIDER_TRACK_X   14   /* relative to the card */
#define WT_SLIDER_TRACK_Y  116
#define WT_SLIDER_TRACK_W  276
#define WT_SLIDER_TRACK_H    8
#define WT_SLIDER_KNOB_H    24
#define WT_SLIDER_STEP       5

/* keyboard */
#define WT_KBD_Y           104
#define WT_KBD_H           120
#define WT_KEY_W            30
#define WT_KEY_H            26
#define WT_KEY_GAP           2
#define WT_KEY_ROW_Y0      108   /* rows at 108 + 28n */
#define WT_KEY_ROW_STEP     28
#define WT_KEY_WIDE_W       46   /* SHIFT, DELETE, ENTER, 123 */
#define WT_KEY_SPACE_W     126
#define WT_KEY_ROW2_INSET   16

/* touch calibration */
#define WT_XHAIR_SIZE       40
#define WT_XHAIR_INSET      24   /* corners 24 px in */

/* motion */
#define WT_ANIM_MS         150
#define WT_POWER_CONFIRM_MS 5000

/* ---- palettes ---- */
typedef struct {
    uint32_t bg, surface, line, text, dim, highlight, dark;
} wt_palette_t;

#define WT_PAL_NIGHT  { 0x05070D, 0x0B1020, 0x1D2742, 0xE4F0FF, 0x6F7D9C, 0x2E3C5E, 0x02030A }
#define WT_PAL_VOID   { 0x000000, 0x0A0A0A, 0x1E1E1E, 0xF0F0F0, 0x777777, 0x333333, 0x000000 }
#define WT_PAL_DUSK   { 0x0A0512, 0x150B24, 0x2D1A4A, 0xF2E8FF, 0x8A74AD, 0x43306A, 0x05020A }
#define WT_PAL_AERO   { 0x04101E, 0x0C2240, 0x1E3F6B, 0xF0F8FF, 0x86A9D1, 0x2E5A8F, 0x02070F }
#define WT_PAL_TAPE   { 0x14110D, 0x262019, 0x3D3328, 0xF2E6CF, 0xA08F75, 0x5A4B3A, 0x0B0907 }

/* ---- accents ---- */
typedef struct {
    uint32_t accent, accent2, warn;
} wt_accent_t;

#define WT_ACC_CYAN    { 0x00E5FF, 0xFF2BD6, 0xFF3355 }
#define WT_ACC_MAGENTA { 0xFF2BD6, 0x00E5FF, 0xFF3355 }
#define WT_ACC_GREEN   { 0x39FF6A, 0x00E5FF, 0xFF3355 }
#define WT_ACC_AMBER   { 0xFFB000, 0xFF3D6E, 0xFF3355 }
#define WT_ACC_RED     { 0xFF3355, 0xFFB000, 0xFFB000 }

/* fixed colours */
#define WT_LAYER_O     0xFF8A1F   /* orange key layer */
#define WT_LAYER_B     0x4DA3FF   /* blue key layer; G uses the palette's dim */

/* ---- derived alpha tokens (blend <colour> over what is behind, at this opacity) ---- */
#define WT_OPA_ACCENT_FILL    41   /* 16 %  accent over surface: focused fill */
#define WT_OPA_ACCENT_PRESSED 89   /* 35 %  accent over surface: pressed fill */
#define WT_OPA_ACCENT_GLOW    89   /* 35 %  accent: 1 px outer focus line */
#define WT_OPA_TEXT_HILIGHT   38   /* 15 %  text: CYBER 1 px top edge */
#define WT_OPA_TEXT_EDGE      20   /*  8 %  text: CYBER idle border, AERO gloss band */
#define WT_OPA_TEXT_BRIGHT   179   /* 70 %  text: CYBER corner pieces, battery outline */
#define WT_OPA_TEXT_TICK      64   /* 25 %  text: CYBER ticks, idle corner notch */
#define WT_OPA_TEXT_WHITE    204   /* 80 %  text: AERO focus ring and icon rings */
#define WT_OPA_DISABLED      115   /* 45 %  whole element */

/* NUKE derives its neutrals from the accent (palette supplies bg only) */
#define WT_OPA_NUKE_DIM      140   /* 55 % accent: dim text */
#define WT_OPA_NUKE_LINE      77   /* 30 % accent: lines */
#define WT_OPA_NUKE_SURFACE   31   /* 12 % accent: surfaces */
#define WT_OPA_NUKE_FILL      51   /* 20 % accent: focused fill */

/* ---- styles ---- */
typedef enum { WT_STYLE_CYBER, WT_STYLE_AERO, WT_STYLE_DECK, WT_STYLE_NUKE } wt_style_t;
typedef enum { WT_PALETTE_AUTO, WT_PALETTE_NIGHT, WT_PALETTE_VOID, WT_PALETTE_DUSK, WT_PALETTE_AERO, WT_PALETTE_TAPE } wt_palette_id_t;
typedef enum { WT_ACCENT_AUTO, WT_ACCENT_CYAN, WT_ACCENT_MAGENTA, WT_ACCENT_GREEN, WT_ACCENT_AMBER, WT_ACCENT_RED } wt_accent_id_t;

/* AUTO resolves to the style's defaults */
#define WT_STYLE_DEFAULTS { \
    /* CYBER */ { WT_PALETTE_NIGHT, WT_ACCENT_CYAN  }, \
    /* AERO  */ { WT_PALETTE_AERO,  WT_ACCENT_CYAN  }, \
    /* DECK  */ { WT_PALETTE_TAPE,  WT_ACCENT_AMBER }, \
    /* NUKE  */ { WT_PALETTE_VOID,  WT_ACCENT_GREEN } }

/* per-style numbers that differ (everything else is shared) */
typedef struct {
    uint8_t radius_tile, radius_row, radius_key;   /* px */
    uint8_t border_idle, border_focus;              /* px */
    uint8_t font_title, font_name, font_data, font_big; /* px */
    uint8_t tracking_title, tracking_name;          /* px letter spacing */
} wt_style_metrics_t;

#define WT_STYLE_METRICS { \
    /* CYBER */ { 0, 0, 0,   1, 1,   16, 14, 12, 28,  1, 1 }, \
    /* AERO  */ {12,18,13,   1, 2,   12, 12, 12, 26,  1, 0 }, \
    /* DECK  */ { 3, 3, 3,   1, 1,   14, 13, 12, 28,  1, 1 }, /* plus 2 px dark bevel edges */ \
    /* NUKE  */ { 0, 0, 0,   1, 2,   16, 16, 12, 32,  0, 0 } }

/* fonts to compile (Google Fonts, SIL OFL) */
/* CYBER: Share Tech Mono 12, 14, 16, 28
 * AERO : Audiowide 12, 26; Chakra Petch 12, 14 (semi-bold)
 * DECK : Aldrich 13, 14; Space Mono 12, 16, 28 (regular + bold at 12/28)
 * NUKE : DotGothic16 16, 32; Share Tech Mono 12
 */

#endif /* WT_TOKENS_H */
