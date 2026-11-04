/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM PC
 *          systems and compatibles from 1981 through fairly recent system
 *          designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          The Turbo Vision application: menu bar, status line, desktop
 *          with the guest screen (and later serial console windows).
 *
 *
 *
 * Authors: The 86Box TUI frontend contributors.
 *
 *          Copyright 2026.
 */

#ifndef TUI_APP_HPP
#define TUI_APP_HPP

#define Uses_TApplication
#define Uses_TProgInit
#define Uses_TMenuBar
#define Uses_TSubMenu
#define Uses_TMenuItem
#define Uses_TStatusLine
#define Uses_TStatusDef
#define Uses_TStatusItem
#define Uses_TDeskTop
#define Uses_TRect
#define Uses_TEvent
#define Uses_TEventQueue
#define Uses_TKeys
#define Uses_TDialog
#define Uses_TDrawBuffer
#define Uses_TStaticText
#define Uses_TButton
#include <tvision/tv.h>
#define Uses_TBackground /* tv.h only forward-declares it */
#include <tvision/app.h>

#include "tui_screen.hpp"

/* Custom command codes (standard commands end below 100; stddlg uses
   1001-1002). */
enum {
    cmToggleCleanMode = 2000,
    cmPause,
    cmHardReset,
    cmCAD,
    cmFastForward,
    cmPowerOff,
    cmAbout,
    cmFloppyMount,   /* + drive number */
    cmFloppyEject,   /* + drive number */
    cmFloppyWP,      /* + drive number */
    cmCDROMMount,    /* + drive number */
    cdCDROMEject,    /* + drive number */
    cmSerialWindow,  /* + port number */
    cmUpdateStatus,
    cmExitApp        /* menu Exit: asks for confirmation first; the actual
                        loop exit is the standard cmQuit, posted by idle()
                        once tui_exit_event is set */
};

/* Menu bar and status line that ignore keyboard events while hidden, so
   that clean mode passes every key (except our reserved toggle) through
   to the guest. */
class TUIMenuBar : public TMenuBar {
public:
    TUIMenuBar(const TRect &bounds, TSubMenu &aMenu) noexcept :
        TMenuBar(bounds, aMenu) {}

    virtual void handleEvent(TEvent &event)
    {
        if (!(state & sfVisible) && event.what == evKeyDown)
            return;
        TMenuBar::handleEvent(event);
    }
};

class TUIStatusLine : public TStatusLine {
public:
    TUIStatusLine(const TRect &bounds, TStatusDef &aDefs) noexcept :
        TStatusLine(bounds, aDefs) {}

    virtual void handleEvent(TEvent &event)
    {
        if (!(state & sfVisible) && event.what == evKeyDown)
            return;
        TStatusLine::handleEvent(event);
    }

    /* Everything on the status line is drawn by us; the TStatusDef items
       exist only for the F10 -> cmMenu key conversion. */
    virtual void draw();
    virtual void update() {}
};

class TUIApp : public TApplication {
public:
    TUIApp();

    virtual void handleEvent(TEvent &event);
    virtual void idle();
    virtual void changeBounds(const TRect &bounds);

    static TMenuBar    *initMenuBar(TRect r);
    static TStatusLine *initStatusLine(TRect r);

    void setCleanMode(bool on);
    bool cleanMode() const { return clean_mode; }

    TUIGuestScreen *guestView = nullptr;

    /* Pending work for the UI thread, posted from other threads. */
    void postStatusUpdate();

    void applyLayout();

private:
    bool clean_mode = false;
};

extern TUIApp *tui_app;

/* Status line contents, updated from any thread, applied in idle(). */
struct TUIStatusIcon {
    int tag;    /* SB_* | id, as passed to the ui_sb_* functions */
    int active; /* activity blink */
    int write;  /* write activity */
    int present;/* media present (0 = ejected) */
    int wp;     /* write-protected */
};

struct TUIStatusState {
    char           message[160]; /* ui_sb_set_text / ui_sb_bugui / messages */
    char           mt32lcd[40];
    int            speed_percent = -1;
    int            paused        = 0;
    int            ready         = -1;
    TUIStatusIcon  icons[20];
    int            icon_count = 0;
    int            dirty      = 0;
};
extern TUIStatusState tui_status;

#endif /* TUI_APP_HPP */