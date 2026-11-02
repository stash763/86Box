/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM PC
 *          systems and compatibles from 1981 through fairly recent system
 *          designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Serial console window: a VT/ANSI terminal (libvterm) attached
 *          to a guest COM port through the `tui` char device backend.
 *
 *
 *
 * Authors: The 86Box TUI frontend contributors.
 *
 *          Copyright 2026.
 */

#ifndef TUI_SERIAL_HPP
#define TUI_SERIAL_HPP

#define Uses_TView
#define Uses_TWindow
#define Uses_TFrame
#define Uses_TRect
#define Uses_TEvent
#define Uses_TKeys
#define Uses_TDrawBuffer
#define Uses_TButton
#define Uses_TDialog
#define Uses_TStaticText
#include <tvision/tv.h>

#include <vterm.h>

#include "tui_frontend.h"

/* The terminal surface inside a serial window. */
class TUISerialView : public TView {
public:
    TUISerialView(const TRect &bounds, int port);
    ~TUISerialView();

    virtual void draw();
    virtual void handleEvent(TEvent &event);
    virtual void setState(ushort aState, Boolean enable);

    /* Feed guest output; called on the UI thread. */
    void receive(const uint8_t *data, size_t len);
    /* Redraw damaged cells (called after receive). */
    void flushDamage();
    /* Update the window title with the port settings. */
    void updateTitle();

    int port_num;

    /* Accessed by the static libvterm callbacks below. */
    static VTermScreenCallbacks screen_callbacks;

    VTerm       *vt  = nullptr;
    VTermScreen *vts = nullptr;

    int  damage[TUI_TEXT_MAX_ROWS][2]; /* [row] = {mincol, maxcol+1} */
    bool any_damage = false;
    int  pen_row = 0;
    int  pen_col = 0;

    /* Pending input bytes for the guest, produced by vterm's output
       callback (keyboard input). */
    uint8_t outbuf[256];
    int     outlen = 0;

private:
    void sendKey(const KeyDownEvent &key);
};

/* The window holding the terminal view. */
class TUISerialWindow : public TWindow {
public:
    TUISerialWindow(int port);
    ~TUISerialWindow();

    TUISerialView *term = nullptr;
    int port_num;
};

/* Per-port state shared with the C glue (tui_serial_output etc.). */
struct TUISerialPort {
    int  attached  = 0;
    int  window_open = 0;
    int  user_closed = 0; /* do not auto-reopen until reattach or menu */
    TUISerialWindow *window = nullptr;

    /* Guest -> UI ring. */
    uint8_t tx_ring[65536];
    volatile size_t tx_head = 0;
    volatile size_t tx_tail = 0;

    /* UI -> guest ring. */
    uint8_t rx_ring[65536];
    volatile size_t rx_head = 0;
    volatile size_t rx_tail = 0;

    /* Line state. */
    uint32_t guest_control = 0; /* DTR/RTS/break set by the guest */
};

extern TUISerialPort tui_serial_ports[TUI_SERIAL_PORTS];

#endif /* TUI_SERIAL_HPP */