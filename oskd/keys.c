#include "keys.h"
#include "vkbd.h"

#include <linux/input-event-codes.h>

/* Row 1: number row. */
static const keydef row1[] = {
    { KEY_GRAVE, NULL, 4, KT_CHAR, 0 },
    { KEY_1, NULL, 4, KT_CHAR, 0 },
    { KEY_2, NULL, 4, KT_CHAR, 0 },
    { KEY_3, NULL, 4, KT_CHAR, 0 },
    { KEY_4, NULL, 4, KT_CHAR, 0 },
    { KEY_5, NULL, 4, KT_CHAR, 0 },
    { KEY_6, NULL, 4, KT_CHAR, 0 },
    { KEY_7, NULL, 4, KT_CHAR, 0 },
    { KEY_8, NULL, 4, KT_CHAR, 0 },
    { KEY_9, NULL, 4, KT_CHAR, 0 },
    { KEY_0, NULL, 4, KT_CHAR, 0 },
    { KEY_MINUS, NULL, 4, KT_CHAR, 0 },
    { KEY_EQUAL, NULL, 4, KT_CHAR, 0 },
    { KEY_BACKSPACE, "⌫", 8, KT_ACTION, 0 },
};

/* Row 2. */
static const keydef row2[] = {
    { KEY_TAB, "⇥", 6, KT_ACTION, 0 },
    { KEY_Q, NULL, 4, KT_CHAR, 0 },
    { KEY_W, NULL, 4, KT_CHAR, 0 },
    { KEY_E, NULL, 4, KT_CHAR, 0 },
    { KEY_R, NULL, 4, KT_CHAR, 0 },
    { KEY_T, NULL, 4, KT_CHAR, 0 },
    { KEY_Y, NULL, 4, KT_CHAR, 0 },
    { KEY_U, NULL, 4, KT_CHAR, 0 },
    { KEY_I, NULL, 4, KT_CHAR, 0 },
    { KEY_O, NULL, 4, KT_CHAR, 0 },
    { KEY_P, NULL, 4, KT_CHAR, 0 },
    { KEY_LEFTBRACE, NULL, 4, KT_CHAR, 0 },
    { KEY_RIGHTBRACE, NULL, 4, KT_CHAR, 0 },
    { KEY_ENTER, "⏎", 6, KT_ACTION, 0 },
};

/* Row 3. */
static const keydef row3[] = {
    { KEY_CAPSLOCK, "⇪", 7, KT_MOD, VKBD_CAPS },
    { KEY_A, NULL, 4, KT_CHAR, 0 },
    { KEY_S, NULL, 4, KT_CHAR, 0 },
    { KEY_D, NULL, 4, KT_CHAR, 0 },
    { KEY_F, NULL, 4, KT_CHAR, 0 },
    { KEY_G, NULL, 4, KT_CHAR, 0 },
    { KEY_H, NULL, 4, KT_CHAR, 0 },
    { KEY_J, NULL, 4, KT_CHAR, 0 },
    { KEY_K, NULL, 4, KT_CHAR, 0 },
    { KEY_L, NULL, 4, KT_CHAR, 0 },
    { KEY_SEMICOLON, NULL, 4, KT_CHAR, 0 },
    { KEY_APOSTROPHE, NULL, 4, KT_CHAR, 0 },
    { KEY_BACKSLASH, NULL, 5, KT_CHAR, 0 },
};

/* Row 4, ISO: the extra key sits between shift and Z. */
static const keydef row4_iso[] = {
    { KEY_LEFTSHIFT, "⇧", 6, KT_MOD, VKBD_SHIFT },
    { KEY_102ND, NULL, 4, KT_CHAR, 0 },
    { KEY_Z, NULL, 4, KT_CHAR, 0 },
    { KEY_X, NULL, 4, KT_CHAR, 0 },
    { KEY_C, NULL, 4, KT_CHAR, 0 },
    { KEY_V, NULL, 4, KT_CHAR, 0 },
    { KEY_B, NULL, 4, KT_CHAR, 0 },
    { KEY_N, NULL, 4, KT_CHAR, 0 },
    { KEY_M, NULL, 4, KT_CHAR, 0 },
    { KEY_COMMA, NULL, 4, KT_CHAR, 0 },
    { KEY_DOT, NULL, 4, KT_CHAR, 0 },
    { KEY_SLASH, NULL, 4, KT_CHAR, 0 },
    { KEY_RIGHTSHIFT, "⇧", 10, KT_MOD, VKBD_SHIFT },
};

/* Row 4, ANSI: no KEY_102ND, so left shift absorbs its width. Drawing a key
 * that types nothing would be worse than not drawing it. */
static const keydef row4_ansi[] = {
    { KEY_LEFTSHIFT, "⇧", 10, KT_MOD, VKBD_SHIFT },
    { KEY_Z, NULL, 4, KT_CHAR, 0 },
    { KEY_X, NULL, 4, KT_CHAR, 0 },
    { KEY_C, NULL, 4, KT_CHAR, 0 },
    { KEY_V, NULL, 4, KT_CHAR, 0 },
    { KEY_B, NULL, 4, KT_CHAR, 0 },
    { KEY_N, NULL, 4, KT_CHAR, 0 },
    { KEY_M, NULL, 4, KT_CHAR, 0 },
    { KEY_COMMA, NULL, 4, KT_CHAR, 0 },
    { KEY_DOT, NULL, 4, KT_CHAR, 0 },
    { KEY_SLASH, NULL, 4, KT_CHAR, 0 },
    { KEY_RIGHTSHIFT, "⇧", 10, KT_MOD, VKBD_SHIFT },
};

/* Row 5: modifiers, space, arrows. Super carries no glyph of its own on the
 * FW12 -- the physical key is the Framework logo. */
static const keydef row5[] = {
    { KEY_LEFTCTRL, "Ctrl", 6, KT_MOD, VKBD_CTRL },
    { KEY_LEFTMETA, "❖", 4, KT_MOD, VKBD_SUPER },
    { KEY_LEFTALT, "Alt", 4, KT_MOD, VKBD_ALT },
    { KEY_SPACE, " ", 20, KT_CHAR, 0 },
    { KEY_RIGHTALT, "AltGr", 5, KT_MOD, VKBD_ALTGR },
    { KEY_LEFT, "←", 4, KT_ACTION, 0 },
    { KEY_UP, "↑", 4, KT_ACTION, 0 },
    { KEY_DOWN, "↓", 4, KT_ACTION, 0 },
    { KEY_RIGHT, "→", 4, KT_ACTION, 0 },
};

#define NELEMS(a) ((int)(sizeof(a) / sizeof((a)[0])))

int keys_rows(bool iso, const keyrow **rows_out)
{
    static keyrow rows[5];

    rows[0] = (keyrow){ row1, NELEMS(row1) };
    rows[1] = (keyrow){ row2, NELEMS(row2) };
    rows[2] = (keyrow){ row3, NELEMS(row3) };
    rows[3] = iso ? (keyrow){ row4_iso, NELEMS(row4_iso) }
                  : (keyrow){ row4_ansi, NELEMS(row4_ansi) };
    rows[4] = (keyrow){ row5, NELEMS(row5) };

    *rows_out = rows;
    return 5;
}
