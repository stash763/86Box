/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM PC
 *          systems and compatibles from 1981 through fairly recent system
 *          designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Guest text screen view: renders the emulated machine's text-mode
 *          screen as real terminal cells.
 *
 *
 *
 * Authors: The 86Box TUI frontend contributors.
 *
 *          Copyright 2026.
 */

#ifndef TUI_SCREEN_HPP
#define TUI_SCREEN_HPP

#define Uses_TView
#define Uses_TRect
#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TKeys
#include <tvision/tv.h>

#include "tui_keymap.h"

class TUIGuestScreen : public TView {
public:
    TUIGuestScreen(const TRect &bounds);

    virtual void draw();
    virtual void handleEvent(TEvent &event);
    virtual void setState(ushort aState, Boolean enable);

    /* Pull the latest snapshot and redraw if it changed. UI thread. */
    void update();

    /* Borderless mode: the guest screen is drawn edge to edge with no
       title or frame (for clean mode and harness-driven runs). */
    void setBorderless(bool on)
    {
        borderless = on;
        drawView();
    }

    /* Send breaks for keys we told the guest are held (focus loss). */
    void releaseAllKeys();

    /* Deferred key-break bookkeeping (called from the app's idle). */
    void tick();

private:
    void flushBreaks();
    void sendSequence(const TuiKeySequence &seq);

    struct tui_text_snap snap;
    uint64_t             last_drawn_frame;
    bool                 have_snapshot;
    bool                 borderless;
    int                  blink_countdown; /* cursor blink phase */

    TuiKeySequence pending_breaks;
    bool           breaks_pending;
    uint32_t       breaks_at;
};

#endif /* TUI_SCREEN_HPP */