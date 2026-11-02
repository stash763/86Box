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
 *          tvision key codes follow the Borland convention:
 *              keyCode = (scanCode << 8) | charCode
 *          so for most keys the physical XT set-1 scancode is simply the
 *          high byte of keyCode. E0-prefixed (gray) keys are reported in
 *          the 0x100|scan encoding keyboard_input() expects, matching the
 *          sdl_to_xt table of the SDL frontend.
 *
 *
 *
 * Authors: The 86Box TUI frontend contributors.
 *
 *          Copyright 2026.
 */

#include <string.h>

#include "tui_keymap.h"

/* E0-prefixed keys: tvision reports the keypad/gray scan code in the high
   byte; keyboard_input() wants 0x100 | scan. */
static uint16_t
xlate_scan(uint16_t keyCode)
{
    uint16_t scan = keyCode >> 8;

    switch (scan) {
        /* Gray keys that are always E0-prefixed on a real keyboard. */
        case 0x47: /* Home */
        case 0x48: /* Up */
        case 0x49: /* PgUp */
        case 0x4b: /* Left */
        case 0x4d: /* Right */
        case 0x4f: /* End */
        case 0x50: /* Down */
        case 0x51: /* PgDn */
        case 0x52: /* Ins */
        case 0x53: /* Del */
            return 0x100 | scan;

        /* Keypad keys that are NOT E0-prefixed (the guest handles NumLock
           itself, so always send the normal codes). */
        case 0x37: /* gray * */
        case 0x4a: /* gray - */
        case 0x4e: /* gray + */
            return scan;

        default:
            return scan;
    }
}

/* Fallback for keys tvision reports with scan code 0: derive the scan code
   from the CP437 character. This covers terminals that only report the
   character (some Ctrl+key combos, Unicode-only input). */
static uint16_t
scan_from_char(uint16_t keyCode, ushort controlKeyState)
{
    uint8_t ch = keyCode & 0xff;

    if (ch >= 'a' && ch <= 'z')
        ch -= 0x20;
    if (ch >= 'A' && ch <= 'Z') {
        static const uint8_t letter_scan[26] = {
            0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, /* A-J */
            0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1f, 0x14, /* K-T */
            0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c                          /* U-Z */
        };
        return letter_scan[ch - 'A'];
    }
    if (ch >= '1' && ch <= '9')
        return 0x01 + (ch - '1');
    switch (ch) {
        case '0': return 0x0b;
        case '!': return 0x02;
        case '@': return 0x03;
        case '#': return 0x04;
        case '$': return 0x05;
        case '%': return 0x06;
        case '^': return 0x07;
        case '&': return 0x08;
        case '*': return 0x09;
        case '(': return 0x0a;
        case ')': return 0x0b;
        case '-': case '_': return 0x0c;
        case '=': case '+': return 0x0d;
        case 0x08: case 0x7f: return 0x0e; /* Backspace */
        case '\t': return 0x0f;
        case '\r': case '\n': return 0x1c;
        case ' ': return 0x39;
        case '[': case '{': return 0x1a;
        case ']': case '}': return 0x1b;
        case ';': case ':': return 0x27;
        case '\'': case '"': return 0x28;
        case '`': case '~': return 0x29;
        case '\\': case '|': return 0x2b;
        case ',': case '<': return 0x33;
        case '.': case '>': return 0x34;
        case '/': case '?': return 0x35;
        default:
            /* Control codes map onto the letter row (Ctrl+C = 0x03 etc.). */
            if ((controlKeyState & kbCtrlShift) && ch < 0x20) {
                uint8_t letter = (ch == 0x0a) ? 'J' : (ch + '@');
                if (letter >= 'A' && letter <= 'Z')
                    return scan_from_char(letter, 0);
            }
            return 0;
    }
}

/* Modifier synthesis: the guest's keyboard controller expects make/break
   codes for the modifier keys themselves. Track the state we last told the
   guest and emit transitions around the actual key. Terminals do not tell
   left from right, so the left scancode is used. */
enum {
    MOD_SHIFT = 1,
    MOD_CTRL  = 2,
    MOD_ALT   = 4
};

static int last_mod_state = 0;

static void
push_make(TuiKeySequence &out, uint16_t scan)
{
    if (out.make_count < (int) (sizeof(out.makes) / sizeof(out.makes[0])))
        out.makes[out.make_count++] = scan;
}

static void
push_break(TuiKeySequence &out, uint16_t scan)
{
    if (out.break_count < (int) (sizeof(out.breaks) / sizeof(out.breaks[0])))
        out.breaks[out.break_count++] = scan;
}

bool
tui_key_translate(const KeyDownEvent &key, TuiKeySequence &out)
{
    out.make_count  = 0;
    out.break_count = 0;

    /* Map tvision's controlKeyState to our modifier bits. */
    int new_state = 0;
    if (key.controlKeyState & kbShift)
        new_state |= MOD_SHIFT;
    if (key.controlKeyState & kbCtrlShift)
        new_state |= MOD_CTRL;
    if (key.controlKeyState & kbAltShift)
        new_state |= MOD_ALT;

    static const struct {
        int      bit;
        uint16_t scan;
    } mods[] = {
        { MOD_SHIFT, 0x2a },
        { MOD_CTRL,  0x1d },
        { MOD_ALT,   0x38 }
    };

    /* Modifier makes before the key. */
    for (auto &m : mods)
        if ((new_state & m.bit) && !(last_mod_state & m.bit))
            push_make(out, m.scan);

    /* The key itself. */
    bool sent_key = false;
    if (key.keyCode != 0 && key.keyCode != kbNoKey) {
        uint16_t scan = xlate_scan(key.keyCode);
        if (scan == 0)
            scan = scan_from_char(key.keyCode, key.controlKeyState);
        if (scan) {
            push_make(out, scan);
            push_break(out, scan); /* synthesized release */
            sent_key = true;
        }
    }

    /* Modifier breaks after the key. */
    for (auto &m : mods)
        if (!(new_state & m.bit) && (last_mod_state & m.bit))
            push_break(out, m.scan);

    last_mod_state = new_state;

    return sent_key || (out.make_count > 0);
}

void
tui_key_release_all(TuiKeySequence &out)
{
    out.make_count  = 0;
    out.break_count = 0;

    static const struct {
        int      bit;
        uint16_t scan;
    } mods[] = {
        { MOD_SHIFT, 0x2a },
        { MOD_CTRL,  0x1d },
        { MOD_ALT,   0x38 }
    };

    for (auto &m : mods)
        if (last_mod_state & m.bit)
            push_break(out, m.scan);
    last_mod_state = 0;
}