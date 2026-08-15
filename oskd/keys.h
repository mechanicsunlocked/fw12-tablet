/* The physical key table for the on-screen keyboard.
 *
 * Geometry lives here rather than in QML on purpose. The daemon already knows
 * whether the active keymap is ISO or ANSI, and it is the only thing that can
 * derive what each key prints. Keeping both in one place means the QML side
 * only has to draw what it is handed, and a key's size, position and label can
 * never disagree with what it types.
 *
 * Widths are in quarter-units: 4 = one standard key. A row is 60 quarter-units
 * wide, matching the Framework 12's own proportions.
 */
#ifndef FW12_KEYS_H
#define FW12_KEYS_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    KT_CHAR,   /* prints something; the label comes from the keymap */
    KT_MOD,    /* latching modifier, e.g. Shift or AltGr */
    KT_ACTION, /* fixed-label key that acts, e.g. Backspace or Enter */
} ktype;

typedef struct {
    uint32_t code;      /* evdev keycode */
    const char *label;  /* fixed label, or NULL to derive from the keymap */
    uint16_t width;     /* quarter-units; 4 = 1u */
    ktype type;
    uint32_t modbit;    /* VKBD_* bit, for KT_MOD */
} keydef;

typedef struct {
    const keydef *keys;
    int count;
} keyrow;

/* Rows of the keyboard for the given body style. `iso` adds the extra key
 * beside left shift and narrows shift to make room; ANSI drops it and widens
 * shift instead, because a key that types nothing is worse than no key. */
int keys_rows(bool iso, const keyrow **rows_out);

#endif
