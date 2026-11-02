/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM PC
 *          systems and compatibles from 1981 through fairly recent system
 *          designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          tvision key code to XT scancode translation.
 *
 *
 *
 * Authors: The 86Box TUI frontend contributors.
 *
 *          Copyright 2026.
 */

#ifndef TUI_KEYMAP_H
#define TUI_KEYMAP_H

#include <stdint.h>

#define Uses_TKeys
#define Uses_TEvent
#include <tvision/tv.h>

/* A translated key press: make codes to send immediately, break codes to
   send a moment later.

   Terminals do not report key release, so a press becomes a make followed
   by a synthesized break. The break is deferred by the caller (~30 ms) so
   that applications which poll the "key held" state still see the key down
   between the terminal's auto-repeat presses. */
struct TuiKeySequence {
    uint16_t makes[8];
    int      make_count;
    uint16_t breaks[8];
    int      break_count;
};

/* Translate a tvision key-down event. Returns true if anything should be
   sent to the guest. Modifier state changes (Ctrl/Alt/Shift) are
   synthesized as make/break codes around the key itself so guests that
   poll the keyboard shift state see the host terminal's state. */
bool tui_key_translate(const KeyDownEvent &key, TuiKeySequence &out);

/* Breaks for every modifier we told the guest is held (focus loss). */
void tui_key_release_all(TuiKeySequence &out);

#endif /* TUI_KEYMAP_H */