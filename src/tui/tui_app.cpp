/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM PC
 *          systems and compatibles from 1981 through fairly recent system
 *          designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          The Turbo Vision application: menu bar, status line, desktop
 *          with the guest screen view, clean mode, and the thread-safe
 *          status/message plumbing used by the ui_* entry points.
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
#include <thread>

/* Turbo Vision (included below) declares stricmp()/strnicmp() itself, so
   plat.h's declarations must be kept out of this translation unit. */
#define PLAT_NO_STRICMP_DECL

/* These 86Box headers wrap themselves in extern "C" for C++. */
#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/thread.h>
#include <86box/tui.h>
#include <86box/ui.h>

#include "tui_frontend.h"
#include "tui_app.hpp"

/* Built in tui_menu.cpp. */
TSubMenu  *tui_menu_build(void);
void       tui_menu_exec_command(int command);
void       tui_action_exit(void);
const char *tui_menu_status_hint(void);

TUIApp         *tui_app = nullptr;
TUIStatusState tui_status;

static mutex_t *status_mtx = nullptr;

/* ---- Status plumbing (any thread -> UI thread) ------------------------ */

static void
status_lock(void)
{
    if (!status_mtx)
        status_mtx = thread_create_mutex();
    thread_wait_mutex(status_mtx);
}

static void
status_unlock(void)
{
    thread_release_mutex(status_mtx);
}

void
TUIApp::postStatusUpdate()
{
    /* The UI thread applies tui_status in idle(); a wake makes it
       immediate. */
    tui_app_wake();
}

void
tui_status_paused(int paused)
{
    status_lock();
    tui_status.paused = paused;
    tui_status.dirty  = 1;
    status_unlock();
    if (tui_app)
        tui_app->postStatusUpdate();
}

void
tui_status_speed(int percent)
{
    status_lock();
    tui_status.speed_percent = percent;
    tui_status.dirty         = 1;
    status_unlock();
    if (tui_app)
        tui_app->postStatusUpdate();
}

void
tui_status_message(const char *msg)
{
    status_lock();
    snprintf(tui_status.message, sizeof(tui_status.message), "%s", msg ? msg : "");
    tui_status.dirty = 1;
    status_unlock();
    if (tui_app)
        tui_app->postStatusUpdate();
}

void
tui_status_bugui(const char *str)
{
    tui_status_message(str);
}

void
tui_status_mt32lcd(const char *str)
{
    status_lock();
    snprintf(tui_status.mt32lcd, sizeof(tui_status.mt32lcd), "%s", str ? str : "");
    tui_status.dirty = 1;
    status_unlock();
    if (tui_app)
        tui_app->postStatusUpdate();
}

/* Find or create the icon tracking entry for a tag. Caller holds the lock. */
static TUIStatusIcon *
status_icon_find(int tag)
{
    for (int i = 0; i < tui_status.icon_count; i++)
        if (tui_status.icons[i].tag == tag)
            return &tui_status.icons[i];
    if (tui_status.icon_count >= (int) (sizeof(tui_status.icons) / sizeof(tui_status.icons[0])))
        return nullptr;
    TUIStatusIcon *icon = &tui_status.icons[tui_status.icon_count++];
    icon->tag     = tag;
    icon->active  = 0;
    icon->write   = 0;
    icon->present = 1;
    icon->wp      = 0;
    return icon;
}

static void
status_icon_update(int tag, int field, int value)
{
    status_lock();
    TUIStatusIcon *icon = status_icon_find(tag);
    if (icon) {
        switch (field) {
            case 0: icon->active  = value; break;
            case 1: icon->write   = value; break;
            case 2: icon->present = !value; break; /* state 1 = ejected */
            case 3: icon->wp      = value; break;
        }
        tui_status.dirty = 1;
    }
    status_unlock();
    if (tui_app)
        tui_app->postStatusUpdate();
}

void
tui_status_icon(int tag, int active)
{
    status_icon_update(tag, 0, active);
}

void
tui_status_icon_write(int tag, int write)
{
    status_icon_update(tag, 1, write);
}

void
tui_status_icon_state(int tag, int state)
{
    status_icon_update(tag, 2, state);
}

void
tui_status_icon_wp(int tag, int state)
{
    status_icon_update(tag, 3, state);
}

void
tui_status_ready(int ready)
{
    status_lock();
    tui_status.ready = ready;
    tui_status.dirty = 1;
    status_unlock();
    if (tui_app)
        tui_app->postStatusUpdate();
}

void
tui_hard_reset_done(void)
{
    /* Nothing special for now. */
}

/* ---- Status line drawing ---------------------------------------------- */

void
TUIStatusLine::draw()
{
    TDrawBuffer b;

    status_lock();
    TUIStatusState st = tui_status;
    status_unlock();

    TColorAttr attr   = getColor(0x30);  /* background */
    TColorAttr hilite = getColor(0x3F);  /* bright on background */

    b.moveChar(0, ' ', attr, size.x);

    int pos = 1;

    /* Speed / pause state. */
    if (st.paused) {
        b.moveStr(pos, "PAUSED", hilite, size.x - pos - 12);
        pos += 8;
    } else if (st.speed_percent >= 0) {
        char speed[32];
        snprintf(speed, sizeof(speed), "%d%%", st.speed_percent);
        b.moveStr(pos, speed, hilite, size.x - pos - 12);
        pos += (int) strlen(speed) + 2;
    }

    /* Drive and device activity icons. */
    for (int i = 0; i < st.icon_count && pos < size.x - 24; i++) {
        const TUIStatusIcon &icon = st.icons[i];
        char label[16];
        int  base = icon.tag & ~0x0f;
        int  idx  = icon.tag & 0x0f;
        switch (base) {
            case SB_FLOPPY:   snprintf(label, sizeof(label), "%c:", 'A' + idx); break;
            case SB_CDROM:    snprintf(label, sizeof(label), "CD%d", idx); break;
            case SB_RDISK:    snprintf(label, sizeof(label), "Z%d", idx); break;
            case SB_MO:       snprintf(label, sizeof(label), "MO%d", idx); break;
            case SB_TAPE:     snprintf(label, sizeof(label), "TP%d", idx); break;
            case SB_HDD:      snprintf(label, sizeof(label), "HDD%d", idx); break;
            case SB_NETWORK:  snprintf(label, sizeof(label), "NET%d", idx); break;
            case SB_SOUND:    snprintf(label, sizeof(label), "SND%d", idx); break;
            case SB_CARTRIDGE: snprintf(label, sizeof(label), "CR%d", idx); break;
            case SB_CASSETTE: snprintf(label, sizeof(label), "CS", idx); break;
            default:          snprintf(label, sizeof(label), "%02X", base >> 4); break;
        }

        char cell[24];
        if (!icon.present)
            snprintf(cell, sizeof(cell), " %s ", label); /* dim: no media */
        else if (icon.write)
            snprintf(cell, sizeof(cell), "[%s]", label); /* write activity */
        else if (icon.active)
            snprintf(cell, sizeof(cell), "<%s>", label); /* read activity */
        else
            snprintf(cell, sizeof(cell), " %s%c", label, icon.wp ? '*' : ' ');

        b.moveStr(pos, cell, icon.active || icon.write ? hilite : attr, size.x - pos - 12);
        pos += (int) strlen(cell) + 1;
    }

    /* MT-32 LCD, if present. */
    if (st.mt32lcd[0] && pos < size.x - 24) {
        char lcd[48];
        snprintf(lcd, sizeof(lcd), "[%s]", st.mt32lcd);
        b.moveStr(pos, lcd, hilite, size.x - pos - 12);
        pos += (int) strlen(lcd) + 2;
    }

    /* Message from the core. */
    if (st.message[0] && pos < size.x - 14) {
        char msg[160];
        snprintf(msg, sizeof(msg), "%s", st.message);
        b.moveStr(pos, msg, attr, size.x - pos - 12);
    }

    /* Right side: menu hint. */
    b.moveStr(size.x - 10, "F10 Menu", hilite, 10);

    writeBuf(0, 0, size.x, 1, b);
}

/* ---- Message box queue (any thread -> UI thread) ---------------------- */

struct TuiMsgboxRequest {
    int                flags;
    char               header[256];
    char               message[4096];
    TuiMsgboxRequest  *next;
    int                result;
    event_t           *done;
};

static mutex_t         *msgbox_mtx = nullptr;
static TuiMsgboxRequest *msgbox_queue = nullptr;

static void
msgbox_lock(void)
{
    if (!msgbox_mtx)
        msgbox_mtx = thread_create_mutex();
    thread_wait_mutex(msgbox_mtx);
}

static void
msgbox_unlock(void)
{
    thread_release_mutex(msgbox_mtx);
}

/* Called from idle() on the UI thread. */
static void
msgbox_process_queue(void)
{
    for (;;) {
        msgbox_lock();
        TuiMsgboxRequest *req = msgbox_queue;
        if (req)
            msgbox_queue = req->next;
        msgbox_unlock();
        if (!req)
            break;

        req->result = tui_show_msgbox(req->flags, req->header, req->message);
        thread_set_event(req->done);
    }
}

int
tui_queue_msgbox(int flags, const char *header, const char *message)
{
    /* Called from any thread except the UI thread. */
    auto *req = new TuiMsgboxRequest();
    req->flags = flags;
    snprintf(req->header, sizeof(req->header), "%s", header ? header : "");
    snprintf(req->message, sizeof(req->message), "%s", message ? message : "");
    req->next   = nullptr;
    req->result = 0;
    req->done   = thread_create_event();

    msgbox_lock();
    req->next   = msgbox_queue;
    /* Append at the end to preserve order. */
    if (!msgbox_queue) {
        msgbox_queue = req;
    } else {
        TuiMsgboxRequest *p = msgbox_queue;
        while (p->next)
            p = p->next;
        p->next = req;
    }
    msgbox_unlock();

    tui_app_wake();
    thread_wait_event(req->done, -1);
    thread_destroy_event(req->done);
    int result = req->result;
    delete req;
    return result;
}

/* ---- The application --------------------------------------------------- */

TUIApp::TUIApp() :
    TProgInit(&TUIApp::initStatusLine,
              &TUIApp::initMenuBar,
              &TUIApp::initDeskTop),
    TApplication()
{
    tui_app = this;

    /* The guest screen covers the whole desktop and draws its own frame. */
    guestView = new TUIGuestScreen(deskTop->getExtent());
    guestView->growMode = 0; /* laid out manually */
    deskTop->insert(guestView);
    guestView->select();

    if (tui_clean_mode)
        setCleanMode(true);
}

TMenuBar *
TUIApp::initMenuBar(TRect r)
{
    r.b.y = r.a.y + 1;
    return new TUIMenuBar(r, *tui_menu_build());
}

TStatusLine *
TUIApp::initStatusLine(TRect r)
{
    r.a.y = r.b.y - 1;
    /* One item: F10 opens the menu. In clean mode the view is hidden and
       our subclass passes key events through untouched. */
    return new TUIStatusLine(r,
        *new TStatusDef(0, 0xFFFF) +
            *new TStatusItem(0, kbF10, cmMenu));
}

void
TUIApp::applyLayout()
{
    if (!guestView)
        return;

    TRect r = clean_mode ? TRect(0, 0, size.x, size.y) : deskTop->getExtent();
    guestView->locate(r);
}

void
TUIApp::setCleanMode(bool on)
{
    if (clean_mode == on)
        return;
    clean_mode     = on;
    /* Keep the global (read by the guest screen to decide which keys
       belong to the TUI) in sync. */
    tui_clean_mode = on ? 1 : 0;

    if (on) {
        if (menuBar)
            menuBar->hide();
        if (statusLine)
            statusLine->hide();
    } else {
        if (menuBar)
            menuBar->show();
        if (statusLine)
            statusLine->show();
    }

    guestView->setBorderless(on);
    applyLayout();
    guestView->select();
    redraw();
}

void
TUIApp::changeBounds(const TRect &bounds)
{
    TApplication::changeBounds(bounds);
    applyLayout();
}

void
TUIApp::handleEvent(TEvent &event)
{
    if (event.what == evKeyDown) {
        /* Alt+F10 toggles clean mode from anywhere (including clean mode,
           where nothing else is reserved). F10 alone is the menu key in
           normal mode (converted by the status line). */
        if (event.keyDown.keyCode == kbAltF10) {
            setCleanMode(!clean_mode);
            clearEvent(event);
            return;
        }
        if (!clean_mode && event.keyDown.keyCode == kbF10) {
            /* Also accept plain F10 here in case the status line item
               did not catch it (it should have). */
            TEvent menuEvent = {};
            menuEvent.what            = evCommand;
            menuEvent.message.command = cmMenu;
            putEvent(menuEvent);
            clearEvent(event);
            return;
        }
    }

    TApplication::handleEvent(event);

    if (event.what == evCommand) {
        switch (event.message.command) {
            case cmToggleCleanMode:
                setCleanMode(!clean_mode);
                clearEvent(event);
                break;
            case cmExitApp:
                /* Menu Exit: confirm (when enabled), then set the flag
                   that makes idle() post the real cmQuit. Intercepting it
                   here keeps the standard cmQuit reserved for "the loop
                   must end now". */
                tui_action_exit();
                clearEvent(event);
                break;
            case cmPause:
            case cmHardReset:
            case cmCAD:
            case cmFastForward:
            case cmPowerOff:
            case cmAbout:
            case cmFloppyMount:
            case cmFloppyEject:
            case cmFloppyWP:
            case cmCDROMMount:
            case cdCDROMEject:
            case cmSerialWindow:
                tui_menu_exec_command(event.message.command);
                clearEvent(event);
                break;
            default:
                break;
        }
    }
}

void
TUIApp::idle()
{
    TApplication::idle();

    /* Modal message boxes requested by other threads. */
    msgbox_process_queue();

    /* Apply status changes. */
    status_lock();
    bool dirty = tui_status.dirty != 0;
    status_unlock();
    if (dirty && statusLine) {
        status_lock();
        tui_status.dirty = 0;
        status_unlock();
        statusLine->drawView();
    }

    /* Guest screen and serial consoles. */
    if (guestView) {
        guestView->update();
        guestView->tick();
    }
    tui_serial_update();

    /* Exit requested from the emulator thread (power off). */
    if (tui_exit_event) {
        TEvent quitEvent = {};
        quitEvent.what            = evCommand;
        quitEvent.message.command = cmQuit;
        putEvent(quitEvent);
    }
}

/* ---- Boot / run / shutdown --------------------------------------------- */

static bool           app_constructed = false;
static std::thread::id ui_thread_id;

int
tui_app_boot(void)
{
    if (app_constructed)
        return 0;

    tui_app = new TUIApp();
    if (!tui_app)
        return -1;

    /* If anything exit()s while the TUI owns the terminal (a fatal() from
       a code path that does not come back), at least restore it. Normal
       shutdown goes through tui_app_shutdown(), which is idempotent. */
    atexit(tui_app_shutdown);

    ui_thread_id   = std::this_thread::get_id();
    app_constructed = true;
    return 0;
}

int
tui_app_run(void)
{
    if (!app_constructed)
        return 1;
    tui_app->run();
    return 0;
}

void
tui_app_shutdown(void)
{
    if (!app_constructed)
        return;
    /* Suspend the console explicitly before tearing the application down.
       On Unix, tvision replaced stderr with a capture pipe (keeping the
       real descriptor close-on-exec); the console suspend is what restores
       it. Without this, an execvp() of a new image (the VM picker restart)
       leaves the new process with a reader-less pipe on fd 2, and the
       dynamic loader dies of SIGPIPE on its first stderr write. Normal
       exits get the same restore via TScreen's static destructor; doing
       it here is idempotent and also covers the restart path. */
    tui_app->suspend();
    delete tui_app;
    tui_app          = nullptr;
    app_constructed  = false;
}

void
tui_app_wake(void)
{
    if (app_constructed)
        TEventQueue::wakeUp();
}

bool
tui_app_is_running(void)
{
    return app_constructed;
}

bool
tui_on_ui_thread(void)
{
    return app_constructed && (std::this_thread::get_id() == ui_thread_id);
}

void
tui_set_clean_mode(int on)
{
    tui_clean_mode = on; /* picked up by the constructor or toggled later */
    if (tui_app)
        tui_app->setCleanMode(on != 0);
}

int
tui_get_clean_mode(void)
{
    return tui_clean_mode;
}