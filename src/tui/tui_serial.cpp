/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM PC
 *          systems and compatibles from 1981 through fairly recent system
 *          designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Serial console window: a VT/ANSI terminal (libvterm) attached
 *          to a guest COM port through the `tui` char device backend
 *          (src/char/char_tui.c). The tvterm project is the reference for
 *          the libvterm-on-tvision plumbing.
 *
 *          Threading: the guest produces bytes on the emulator thread
 *          (tui_serial_output) and consumes typed bytes there too
 *          (tui_serial_input); both go through lock-free single-producer
 *          single-consumer rings. Everything that touches libvterm or
 *          tvision runs on the UI thread.
 *
 *
 *
 * Authors: The 86Box TUI frontend contributors.
 *
 *          Copyright 2026.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Turbo Vision (included below) declares stricmp()/strnicmp() itself, so
   plat.h's declarations must be kept out of this translation unit. */
#define PLAT_NO_STRICMP_DECL

/* The emulator core is C. Most 86Box headers wrap themselves in extern "C"
   when compiled as C++; the ones that do not are wrapped below. */
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/tui.h>
#include <86box/ui.h>

extern "C" {
#include <86box/char.h>
}

#include "tui_frontend.h"
#include "tui_app.hpp"
#include "tui_serial.hpp"

TUISerialPort tui_serial_ports[TUI_SERIAL_PORTS];

/* ---- Ring buffers (SPSC, lock-free) ------------------------------------ */

static inline size_t
ring_next(size_t i)
{
    return (i + 1) % 65536;
}

/* Producer side. */
static size_t
ring_push(uint8_t *ring, volatile size_t *head, volatile size_t *tail, const uint8_t *data, size_t len)
{
    size_t count = 0;

    while (count < len) {
        size_t next = ring_next(*head);
        if (next == *tail) /* full */
            break;
        ring[*head] = data[count++];
        *head        = next;
    }
    return count;
}

/* Consumer side. */
static size_t
ring_pop(uint8_t *ring, volatile size_t *head, volatile size_t *tail, uint8_t *data, size_t len)
{
    size_t count = 0;

    while (count < len && *tail != *head) {
        data[count++] = ring[*tail];
        *tail         = ring_next(*tail);
    }
    return count;
}

static inline size_t
ring_count(volatile size_t *head, volatile size_t *tail)
{
    return (*head >= *tail) ? (*head - *tail) : (65536 - (*tail - *head));
}

/* ---- C glue called from char_tui.c (emulator thread) ------------------- */

extern "C" void
tui_serial_output(int port, const uint8_t *buf, size_t len)
{
    if (port < 1 || port > TUI_SERIAL_PORTS)
        return;
    TUISerialPort *p = &tui_serial_ports[port - 1];
    ring_push(p->tx_ring, &p->tx_head, &p->tx_tail, buf, len);
    /* Make the UI thread drain the ring now. */
    tui_app_wake();
}

extern "C" size_t
tui_serial_input(int port, uint8_t *buf, size_t len)
{
    if (port < 1 || port > TUI_SERIAL_PORTS)
        return 0;
    TUISerialPort *p = &tui_serial_ports[port - 1];
    return ring_pop(p->rx_ring, &p->rx_head, &p->rx_tail, buf, len);
}

extern "C" uint32_t
tui_serial_lines(int port)
{
    if (port < 1 || port > TUI_SERIAL_PORTS)
        return 0;
    /* Null-modem: the terminal is always connected and ready. */
    return CHAR_COM_CTS | CHAR_COM_DSR | CHAR_COM_DCD;
}

extern "C" void
tui_serial_control(int port, uint32_t flags)
{
    if (port < 1 || port > TUI_SERIAL_PORTS)
        return;
    tui_serial_ports[port - 1].guest_control = flags;
}

extern "C" void
tui_serial_attach(int port, int attached)
{
    if (port < 1 || port > TUI_SERIAL_PORTS)
        return;
    tui_serial_ports[port - 1].attached = attached;
    if (attached)
        tui_serial_ports[port - 1].user_closed = 0;

    /* The window itself is opened/closed by tui_serial_update() on the UI
       thread: this can be called from the emulator thread during a hard
       reset, and tvision views must only be touched from the UI thread. */
    tui_app_wake();
}

/* ---- Color mapping: ANSI 16 colors -> BIOS attributes ------------------- */

/* ANSI palette order is black, red, green, yellow/brown, blue, magenta,
   cyan, white; BIOS order is black, blue, green, cyan, red, magenta,
   brown, white. */
static const uint8_t ansi_to_bios[16] = {
    0, 4, 2, 6, 1, 5, 3, 7, /* normal */
    0, 4, 2, 6, 1, 5, 3, 7  /* bright, | 0x08 added below */
};

static uint8_t
vterm_color_to_bios(const VTermColor *col, int bright)
{
    if (VTERM_COLOR_IS_INDEXED(col)) {
        int idx = col->indexed.idx & 0x0f;
        return ansi_to_bios[idx] | ((idx >= 8 || bright) ? 0x08 : 0);
    }
    /* Approximate RGB down to the 16 BIOS colors by luminance and channel
       dominance. Serial consoles are normally indexed anyway. */
    int r = col->rgb.red, g = col->rgb.green, b = col->rgb.blue;
    int lum = (r * 30 + g * 59 + b * 11) / 100;
    int brightbit = (lum >= 0x80) ? 0x08 : 0;
    if (lum < 0x40) {
        if (r > g && r > b) return 0x04 | brightbit; /* red */
        if (g > r && g > b) return 0x02 | brightbit; /* green */
        if (b > r && b > g) return 0x01 | brightbit; /* blue */
        return 0x00;
    }
    if (r > 0xC0 && g > 0xC0 && b < 0x80) return 0x0E; /* yellow */
    if (r > 0xC0 && b > 0xC0) return 0x0D;             /* magenta */
    if (g > 0xC0 && b > 0xC0) return 0x0B;             /* cyan */
    return 0x07 | brightbit;
}

/* ---- TUISerialView ------------------------------------------------------- */

static int
serial_damage(VTermRect rect, void *user)
{
    auto *view = (TUISerialView *) user;

    for (int row = rect.start_row; row < rect.end_row && row < TUI_TEXT_MAX_ROWS; row++) {
        if (row < 0)
            continue;
        if (rect.start_col < view->damage[row][0])
            view->damage[row][0] = rect.start_col;
        if (rect.end_col > view->damage[row][1])
            view->damage[row][1] = rect.end_col;
    }
    view->any_damage = true;
    return 1;
}

static int
serial_moverect(VTermRect dest, VTermRect src, void *user)
{
    /* Treat moves as full damage; simpler and correct. */
    VTermRect both = dest;
    if (src.start_row < both.start_row) both.start_row = src.start_row;
    if (src.end_row > both.end_row) both.end_row = src.end_row;
    return serial_damage(both, user);
}

static int
serial_movecursor(VTermPos pos, VTermPos oldpos, int visible, void *user)
{
    auto *view = (TUISerialView *) user;

    view->pen_row = pos.row;
    view->pen_col = pos.col;

    /* Damage old and new cursor cells so their look is refreshed. */
    VTermRect r = { oldpos.row, oldpos.row + 1, oldpos.col, oldpos.col + 1 };
    serial_damage(r, user);
    r.start_row = pos.row;
    r.end_row   = pos.row + 1;
    r.start_col = pos.col;
    r.end_col   = pos.col + 1;
    serial_damage(r, user);
    (void) visible;
    return 1;
}

static int
serial_settermprop(UNUSED(VTermProp prop), UNUSED(VTermValue *val), UNUSED(void *user))
{
    return 1;
}

static int
serial_bell(UNUSED(void *user))
{
    return 1;
}

static int
serial_resize(UNUSED(int rows), UNUSED(int cols), UNUSED(void *user))
{
    return 1;
}

static int
serial_sb_pushline(UNUSED(int cols), UNUSED(const VTermScreenCell *cells), UNUSED(void *user))
{
    return 1; /* scrollback is a future feature */
}

static int
serial_sb_popline(UNUSED(int cols), UNUSED(VTermScreenCell *cells), UNUSED(void *user))
{
    return 0;
}

static int
serial_sb_clear(UNUSED(void *user))
{
    return 1;
}

VTermScreenCallbacks TUISerialView::screen_callbacks = {
    .damage       = serial_damage,
    .moverect     = serial_moverect,
    .movecursor   = serial_movecursor,
    .settermprop  = serial_settermprop,
    .bell         = serial_bell,
    .resize       = serial_resize,
    .sb_pushline  = serial_sb_pushline,
    .sb_popline   = serial_sb_popline,
    .sb_clear     = serial_sb_clear,
};

/* Keyboard input -> bytes for the guest: libvterm encodes them (application
   cursor key mode and friends are handled by it) and calls us back. */
static void
serial_output_cb(const char *s, size_t len, void *user)
{
    auto *view = (TUISerialView *) user;

    if (view->outlen + len > sizeof(view->outbuf))
        len = sizeof(view->outbuf) - view->outlen;
    if (len) {
        memcpy(view->outbuf + view->outlen, s, len);
        view->outlen += len;
    }
}

TUISerialView::TUISerialView(const TRect &bounds, int port) :
    TView(bounds), port_num(port)
{
    options  |= ofSelectable;
    eventMask |= evKeyboard | evBroadcast;
    growMode   = gfGrowHiX | gfGrowHiY;

    for (int r = 0; r < TUI_TEXT_MAX_ROWS; r++) {
        damage[r][0] = TUI_TEXT_MAX_COLS;
        damage[r][1] = 0;
    }

    vt = vterm_new(bounds.b.y - bounds.a.y, bounds.b.x - bounds.a.x);
    vterm_set_utf8(vt, 1);
    vterm_output_set_callback(vt, serial_output_cb, this);
    vts = vterm_obtain_screen(vt);
    vterm_screen_set_callbacks(vts, &screen_callbacks, this);
    vterm_screen_set_damage_merge(vts, VTERM_DAMAGE_SCROLL);
    vterm_screen_reset(vts, 1);

    showCursor();
}

TUISerialView::~TUISerialView()
{
    if (vt)
        vterm_free(vt);
}

void
TUISerialView::receive(const uint8_t *data, size_t len)
{
    vterm_input_write(vt, (const char *) data, len);
}

/* Render every damaged row to the screen buffer. Never calls drawView():
   it runs both from flushDamage() (after guest output) and from draw()
   (which drawView() invokes). */
void
TUISerialView::flushDamage()
{
    /* With VTERM_DAMAGE_SCROLL the damage callback only fires here, so
       flush first, then look at what got damaged. */
    vterm_screen_flush_damage(vts);

    if (!any_damage)
        return;

    /* Redraw whole damaged rows. */
    TDrawBuffer b;
    TColorAttr  blank(0x07);

    for (int row = 0; row < size.y; row++) {
        if (row >= TUI_TEXT_MAX_ROWS || damage[row][0] >= damage[row][1])
            continue;
        int c0 = damage[row][0] < 0 ? 0 : damage[row][0];
        int c1 = damage[row][1] < size.x ? damage[row][1] : size.x;

        b.moveChar(0, ' ', blank, size.x);
        for (int col = c0; col < c1; col++) {
            VTermScreenCell cell;
            VTermPos        pos = { row, col };
            vterm_screen_get_cell(vts, pos, &cell);

            uint8_t biosattr;
            uint8_t fg = vterm_color_to_bios(&cell.fg, cell.attrs.bold);
            uint8_t bg = vterm_color_to_bios(&cell.bg, 0);
            if (cell.attrs.reverse) {
                /* Reversed: swap the nibbles. bg & 0x0f keeps a bright
                   background visible as a bright foreground. */
                biosattr = ((fg & 0x07) << 4) | (bg & 0x0f);
            } else {
                biosattr = ((bg & 0x07) << 4) | (fg & 0x0f);
            }

            uint32_t ch = cell.chars[0];
            uint8_t out;
            if (!ch) {
                out = ' ';
            } else if (ch < 0x80) {
                out = (uint8_t) ch;
            } else {
                /* Map the few common box-drawing/CP437-range codepoints
                   back to CP437; anything else becomes a solid block. */
                switch (ch) {
                    case 0x2500: out = 0xC4; break; /* ─ */
                    case 0x2502: out = 0xB3; break; /* │ */
                    case 0x250C: out = 0xDA; break; /* ┌ */
                    case 0x2510: out = 0xBF; break; /* ┐ */
                    case 0x2514: out = 0xC0; break; /* └ */
                    case 0x2518: out = 0xD9; break; /* ┘ */
                    case 0x2591: out = 0xB0; break; /* ░ */
                    case 0x2592: out = 0xB1; break; /* ▒ */
                    case 0x2593: out = 0xB2; break; /* ▓ */
                    case 0x2588: out = 0xDB; break; /* █ */
                    case 0x00B0: out = 0xF8; break; /* ° */
                    case 0x00B7: out = 0xFA; break; /* · */
                    case 0x2191: out = 0x18; break; /* ↑ */
                    case 0x2193: out = 0x19; break; /* ↓ */
                    case 0x2190: out = 0x1B; break; /* ← */
                    case 0x2192: out = 0x1A; break; /* → */
                    case 0x263A: out = 0x01; break; /* ☺ */
                    default:
                        out = (ch >= 0x2500 && ch < 0x2600) ? 0xFE : '?';
                        break;
                }
            }
            /* Write char and attribute cell by cell: TDrawBuffer's
               moveBuf() interprets its source as a plain char array
               (not as TScreenCell structs), which garbles the screen.
               The blink bit is dropped: terminals are unreliable at
               blinking and a harness wants deterministic output. */
            b.putChar(col, out);
            b.putAttribute(col, TColorAttr(biosattr & 0x7f));
        }
        writeBuf(0, row, size.x, 1, b);

        damage[row][0] = TUI_TEXT_MAX_COLS;
        damage[row][1] = 0;
    }

    any_damage = false;

    /* Terminal cursor at the pen position. */
    if (pen_row >= 0 && pen_row < size.y && pen_col >= 0 && pen_col < size.x) {
        setCursor(pen_col, pen_row);
        setState(sfCursorVis, True);
    } else
        setState(sfCursorVis, False);
}

void
TUISerialView::draw()
{
    /* Full redraw: damage every row and run the normal render path. */
    for (int row = 0; row < size.y && row < TUI_TEXT_MAX_ROWS; row++) {
        damage[row][0] = 0;
        damage[row][1] = size.x;
    }
    any_damage = true;
    flushDamage();
}

void
TUISerialView::updateTitle()
{
    TUISerialWindow *w = (TUISerialWindow *) owner;
    if (!w)
        return;
    TUISerialPort *p = &tui_serial_ports[port_num - 1];
    char title[64];
    snprintf(title, sizeof(title), "COM%d console%s",
             port_num, p->attached ? "" : " (detached)");
    w->title = newStr(title);
    if (w->frame)
        w->frame->drawView();
}

void
TUISerialView::sendKey(const KeyDownEvent &key)
{
    VTermModifier mod = VTERM_MOD_NONE;
    if (key.controlKeyState & kbShift)
        mod = (VTermModifier) (mod | VTERM_MOD_SHIFT);
    if (key.controlKeyState & kbAltShift)
        mod = (VTermModifier) (mod | VTERM_MOD_ALT);
    if (key.controlKeyState & kbCtrlShift)
        mod = (VTermModifier) (mod | VTERM_MOD_CTRL);

    outlen = 0;

    /* Function and special keys. */
    VTermKey vkey = VTERM_KEY_NONE;
    switch (key.keyCode) {
        case kbEnter: vkey = VTERM_KEY_ENTER; break;
        case kbTab: vkey = VTERM_KEY_TAB; break;
        case kbBack: vkey = VTERM_KEY_BACKSPACE; break;
        case kbEsc: vkey = VTERM_KEY_ESCAPE; break;
        case kbUp: vkey = VTERM_KEY_UP; break;
        case kbDown: vkey = VTERM_KEY_DOWN; break;
        case kbLeft: vkey = VTERM_KEY_LEFT; break;
        case kbRight: vkey = VTERM_KEY_RIGHT; break;
        case kbIns: vkey = VTERM_KEY_INS; break;
        case kbDel: vkey = VTERM_KEY_DEL; break;
        case kbHome: vkey = VTERM_KEY_HOME; break;
        case kbEnd: vkey = VTERM_KEY_END; break;
        case kbPgUp: vkey = VTERM_KEY_PAGEUP; break;
        case kbPgDn: vkey = VTERM_KEY_PAGEDOWN; break;
        default:
            if (key.keyCode >= kbF1 && key.keyCode <= kbF10)
                vkey = (VTermKey) VTERM_KEY_FUNCTION(key.keyCode - kbF1 + 1);
            else if (key.keyCode >= kbF11 && key.keyCode <= kbF12)
                vkey = (VTermKey) VTERM_KEY_FUNCTION(key.keyCode - kbF11 + 11);
            break;
    }

    if (vkey != VTERM_KEY_NONE) {
        vterm_keyboard_key(vt, vkey, mod);
    } else {
        /* Text: tvision gives us UTF-8 (or a CP437 charCode). */
        TStringView text = key.getText();
        if (text.size() > 0) {
            /* Decode UTF-8 to codepoints. */
            size_t i = 0;
            while (i < text.size()) {
                uint32_t cp = 0;
                uint8_t  c  = (uint8_t) text[i];
                int      extra = 0;
                if ((c & 0x80) == 0) {
                    cp = c;
                } else if ((c & 0xE0) == 0xC0) {
                    cp = c & 0x1F; extra = 1;
                } else if ((c & 0xF0) == 0xE0) {
                    cp = c & 0x0F; extra = 2;
                } else if ((c & 0xF8) == 0xF0) {
                    cp = c & 0x07; extra = 3;
                }
                i++;
                while (extra-- > 0 && i < text.size()) {
                    cp = (cp << 6) | ((uint8_t) text[i] & 0x3F);
                    i++;
                }
                vterm_keyboard_unichar(vt, cp, mod);
            }
        } else {
            /* No text: fall back to the CP437 charCode. */
            uint8_t ch = key.charScan.charCode;
            if (ch)
                vterm_keyboard_unichar(vt, ch, mod);
        }
    }

    /* Whatever libvterm produced goes to the guest. */
    if (outlen) {
        TUISerialPort *p = &tui_serial_ports[port_num - 1];
        ring_push(p->rx_ring, &p->rx_head, &p->rx_tail, outbuf, outlen);
    }
}

void
TUISerialView::handleEvent(TEvent &event)
{
    TView::handleEvent(event);

    if (event.what == evKeyDown) {
        sendKey(event.keyDown);
        clearEvent(event);
    }
}

void
TUISerialView::setState(ushort aState, Boolean enable)
{
    TView::setState(aState, enable);
}

/* ---- TUISerialWindow ------------------------------------------------------ */

TUISerialWindow::TUISerialWindow(int port) :
    TWindowInit(&TUISerialWindow::initFrame),
    TWindow(TRect(2, 2, 62, 22), "Console", wnNoNumber),
    port_num(port)
{
    flags |= wfMove | wfClose | wfGrow;

    TRect r = getExtent();
    r.grow(-1, -1);
    term = new TUISerialView(r, port);
    insert(term);
    term->select();
}

TUISerialWindow::~TUISerialWindow()
{
    if (port_num >= 1 && port_num <= TUI_SERIAL_PORTS) {
        tui_serial_ports[port_num - 1].window      = nullptr;
        tui_serial_ports[port_num - 1].window_open = 0;
        tui_serial_ports[port_num - 1].user_closed = 1;
    }
}

/* ---- Window management (UI thread) ---------------------------------------- */

void
tui_serial_window_open(int port)
{
    if (port < 1 || port > TUI_SERIAL_PORTS)
        return;
    TUISerialPort *p = &tui_serial_ports[port - 1];

    if (!tui_app)
        return;

    if (!p->attached) {
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "COM%d is not attached to a TUI console.\n\n"
                 "Set \"serial%d_device = tui\" in 86box.cfg (or the serial\n"
                 "port settings) and restart the machine.",
                 port, port);
        tui_show_msgbox(MBX_INFO, "Serial console", msg);
        return;
    }

    if (p->window)
        return;

    p->user_closed = 0;
    auto *w        = new TUISerialWindow(port);
    tui_app->insertWindow(w);
    p->window      = w;
    p->window_open = 1;
    w->term->select();
    w->term->updateTitle();
}

void
tui_serial_window_close(int port)
{
    if (port < 1 || port > TUI_SERIAL_PORTS)
        return;
    TUISerialPort *p = &tui_serial_ports[port - 1];

    if (!p->window)
        return;
    p->window->close();
    p->window      = nullptr;
    p->window_open = 0;
}

void
tui_serial_update(void)
{
    for (int i = 0; i < TUI_SERIAL_PORTS; i++) {
        TUISerialPort *p = &tui_serial_ports[i];

        /* Reconcile window state with backend attach state (deferred from
           tui_serial_attach, which may run on the emulator thread). */
        if (p->attached && !p->window && !p->user_closed && tui_app)
            tui_serial_window_open(i + 1);
        else if (!p->attached && p->window)
            tui_serial_window_close(i + 1);

        if (!p->window || !p->window->term)
            continue;

        /* Guest -> terminal. */
        uint8_t buf[4096];
        size_t  len;
        bool    fed = false;
        while ((len = ring_pop(p->tx_ring, &p->tx_head, &p->tx_tail, buf, sizeof(buf))) > 0) {
            p->window->term->receive(buf, len);
            fed = true;
        }
        if (fed)
            p->window->term->flushDamage();
    }
}