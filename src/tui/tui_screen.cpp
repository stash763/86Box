/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM PC
 *          systems and compatibles from 1981 through fairly recent system
 *          designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Guest text screen view: renders the emulated machine's text-mode
 *          screen (captured from the BIOS data area and text plane by
 *          text_capture.c) as real terminal character cells, and forwards
 *          keyboard input to the emulated keyboard.
 *
 *
 *
 * Authors: The 86Box TUI frontend contributors.
 *
 *          Copyright 2026.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Turbo Vision (included below) declares stricmp()/strnicmp() itself, so
   plat.h's declarations must be kept out of this translation unit. */
#define PLAT_NO_STRICMP_DECL

/* The emulator core is C. Most 86Box headers wrap themselves in extern "C"
   when compiled as C++; the ones that do not are wrapped below. */
#include <86box/86box.h>
#include <86box/keyboard.h>
#include <86box/plat.h>

extern "C" {
#include <86box/version.h>
}

#include "tui_frontend.h"
#include "tui_screen.hpp"

#define CURSOR_BLINK_MS 266 /* guest cursor blink half-period, ~530 ms full */

TUIGuestScreen::TUIGuestScreen(const TRect &bounds) :
    TView(bounds)
{
    memset(&snap, 0, sizeof(snap));
    last_drawn_frame = (uint64_t) -1;
    have_snapshot    = false;
    borderless       = false;
    blink_countdown  = CURSOR_BLINK_MS;
    breaks_pending   = false;
    breaks_at        = 0;

    options  |= ofSelectable;
    eventMask |= evKeyboard | evBroadcast;
    growMode   = gfGrowHiX | gfGrowHiY;

    showCursor();
}

void
TUIGuestScreen::update()
{
    uint64_t before = snap.frame;

    tui_text_snapshot(&snap);
    have_snapshot = true;

    /* Redraw when a new frame arrived (or on the first update). */
    if (snap.frame != before || last_drawn_frame == (uint64_t) -1) {
        last_drawn_frame = snap.frame;
        drawView();
    }
}

void
TUIGuestScreen::draw()
{
    TDrawBuffer b;

    /* Content area: inside a one-cell frame unless borderless. */
    int x0 = borderless ? 0 : 1;
    int y0 = borderless ? 0 : 1;
    int w  = size.x - (borderless ? 0 : 2);
    int h  = size.y - (borderless ? 0 : 2);

    if (w <= 0 || h <= 0)
        return;

    if (!borderless) {
        /* Frame + title. */
        TColorAttr frame = getColor(0x03);
        b.moveChar(0, '\xCD', frame, size.x); /* ═ */
        b.putChar(0, (uchar) '\xC9');         /* ╔ */
        b.putChar(size.x - 1, (uchar) '\xBB');/* ╗ */
        b.moveStr(2, EMU_NAME " - Guest Screen", getColor(0x1F), size.x - 4);
        writeBuf(0, 0, size.x, 1, b);

        for (int y = 1; y < size.y - 1; y++) {
            b.moveChar(0, ' ', frame, size.x);
            b.putChar(0, (uchar) '\xBA');       /* ║ */
            b.putChar(size.x - 1, (uchar) '\xBA');
            writeBuf(0, y, size.x, 1, b);
        }

        b.moveChar(0, '\xCD', frame, size.x); /* ═ */
        b.putChar(0, (uchar) '\xC8');         /* ╚ */
        b.putChar(size.x - 1, (uchar) '\xBC');/* ╝ */
        writeBuf(0, size.y - 1, size.x, 1, b);
    }

    if (!have_snapshot || !snap.valid) {
        /* No text screen (graphics mode, or nothing captured yet). */
        const char *msg = have_snapshot
            ? "Graphics mode - not rendered in the TUI"
            : "Starting...";
        TColorAttr attr = borderless ? TColorAttr(0x07) : TColorAttr(getColor(0x0F));
        int len  = (int) strlen(msg);
        int msgx = (w > len) ? (w - len) / 2 : 0;
        int msgy = h / 2;
        for (int y = 0; y < h; y++) {
            b.moveChar(0, ' ', attr, w);
            if (y == msgy)
                b.moveStr(msgx, msg, attr, w);
            writeBuf(x0, y0 + y, w, 1, b);
        }
        setState(sfCursorVis, False);
        return;
    }

    /* Center the guest screen in the content area. */
    int gw = snap.cols;
    int gh = snap.rows;
    int gx = (w > gw) ? (w - gw) / 2 : 0;
    int gy = (h > gh) ? (h - gh) / 2 : 0;

    TColorAttr blank(0x07);

    for (int y = 0; y < h; y++) {
        int grow = y - gy; /* guest row for this view row */
        b.moveChar(0, ' ', blank, w);
        if (grow >= 0 && grow < gh) {
            int ccount = (gw < w) ? gw : w;
            for (int c = 0; c < ccount; c++) {
                uint8_t ch   = snap.chars[grow][c];
                uint8_t attr = snap.attrs[grow][c];
                if (!ch)
                    ch = ' ';
                /* Write char and attribute cell by cell: TDrawBuffer's
                   moveBuf() interprets its source as a plain char array
                   (not as TScreenCell structs), which garbles the
                   screen. */
                b.putChar(gx + c, ch);
                /* The blink bit is rendered as a steady (non-blinking)
                   cell; terminals are unreliable at blinking and a
                   harness wants deterministic output. */
                b.putAttribute(gx + c, TColorAttr(attr & 0x7f));
            }
        }
        writeBuf(x0, y0 + y, w, 1, b);
    }

    /* Hardware cursor: let the terminal draw it so that a terminal-state
       harness reads the guest's cursor position directly. */
    if (snap.cur_row >= 0 && snap.cur_row < gh && snap.cur_col >= 0 && snap.cur_col < gw) {
        setCursor(x0 + gx + snap.cur_col, y0 + gy + snap.cur_row);
        setState(sfCursorVis, True);
    } else {
        setState(sfCursorVis, False);
    }
}

void
TUIGuestScreen::sendSequence(const TuiKeySequence &seq)
{
    for (int i = 0; i < seq.make_count; i++)
        keyboard_input(1, seq.makes[i]);
    for (int i = 0; i < seq.break_count; i++)
        keyboard_input(0, seq.breaks[i]);
}

void
TUIGuestScreen::flushBreaks()
{
    if (!breaks_pending)
        return;
    for (int i = 0; i < pending_breaks.break_count; i++)
        keyboard_input(0, pending_breaks.breaks[i]);
    pending_breaks.break_count = 0;
    breaks_pending             = false;
}

void
TUIGuestScreen::tick()
{
    /* Release deferred key breaks a moment after the press so that
       applications polling the "key held" state still see it. */
    if (breaks_pending && (int32_t) (plat_get_ticks() - breaks_at) >= 30)
        flushBreaks();
}

void
TUIGuestScreen::releaseAllKeys()
{
    TuiKeySequence seq;
    tui_key_release_all(seq);
    flushBreaks();
    for (int i = 0; i < seq.break_count; i++)
        keyboard_input(0, seq.breaks[i]);
}

void
TUIGuestScreen::handleEvent(TEvent &event)
{
    TView::handleEvent(event);

    if (event.what == evKeyDown) {
        /* Alt+F10 (clean mode toggle) always belongs to the TUI, and F10
           is the menu key while the chrome is visible: let those bubble
           up to the application instead of reaching the guest. In clean
           mode the guest gets F10 back. */
        if ((event.keyDown.keyCode == kbAltF10)
            || ((event.keyDown.keyCode == kbF10) && !tui_clean_mode))
            return;

        /* Everything else goes to the guest. */
        TuiKeySequence seq;
        if (tui_key_translate(event.keyDown, seq)) {
            /* Release the previous press's breaks first: the terminal
               only reports presses, so back-to-back presses of the same
               key must not leave a stuck make. */
            flushBreaks();

            for (int i = 0; i < seq.make_count; i++)
                keyboard_input(1, seq.makes[i]);

            if (seq.break_count) {
                pending_breaks = seq;
                breaks_pending = true;
                breaks_at      = plat_get_ticks();
            }
        }
        clearEvent(event);
    }
}

void
TUIGuestScreen::setState(ushort aState, Boolean enable)
{
    TView::setState(aState, enable);

    if ((aState & sfFocused) != 0 && !enable)
        releaseAllKeys();
}