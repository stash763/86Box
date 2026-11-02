/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM PC
 *          systems and compatibles from 1981 through fairly recent system
 *          designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Core ui_* entry points for the TUI frontend: message boxes,
 *          progress waits and status bar plumbing.
 *
 *
 *
 * Authors: The 86Box TUI frontend contributors.
 *
 *          Copyright 2026.
 */

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Turbo Vision (included below) declares stricmp()/strnicmp() itself, so
   plat.h's declarations must be kept out of this translation unit. */
#define PLAT_NO_STRICMP_DECL

/* Classes used by the VM picker; tv.h is included by tui_app.hpp below. */
#define Uses_TListBox
#define Uses_TScrollBar
#define Uses_TStringCollection

/* The emulator core is C. Most 86Box headers wrap themselves in extern "C"
   when compiled as C++; the ones that do not are wrapped below. */
#include <86box/86box.h>
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/ui.h>

extern "C" {
#include <86box/version.h>
}

#include "tui_frontend.h"
#include "tui_app.hpp"

/* ---- Message boxes ----------------------------------------------------- */

/* Wrap message text to a maximum width, returning the line count. */
static int
wrap_text(const char *text, char (*lines)[100], int max_lines, int width)
{
    int   n = 0;
    const char *p = text;

    while (*p && n < max_lines) {
        const char *line_end = strchr(p, '\n');
        int         len      = line_end ? (int) (line_end - p) : (int) strlen(p);

        while (len > width) {
            /* Find the last space before the wrap point. */
            int cut = width;
            while (cut > 0 && p[cut] != ' ')
                cut--;
            if (cut <= 0)
                cut = width;
            snprintf(lines[n], 100, "%.*s", cut, p);
            n++;
            if (n >= max_lines)
                return n;
            p += cut;
            while (*p == ' ')
                p++;
            len = line_end ? (int) (line_end - p) : (int) strlen(p);
        }

        snprintf(lines[n], 100, "%.*s", len, p);
        n++;
        if (n >= max_lines)
            return n;
        if (line_end)
            p = line_end + 1;
        else
            break;
    }
    return n;
}

int
tui_show_msgbox(int flags, const char *header, const char *message)
{
    char lines[24][100];
    int  nlines = wrap_text(message ? message : "", lines, 24, 56);
    int  width  = 60;
    int  height = nlines + 6;

    if (!header) {
        if (flags & MBX_FATAL)
            header = "Fatal error";
        else if (flags & MBX_ERROR)
            header = "Error";
        else if (flags & MBX_WARNING)
            header = "Warning";
        else
            header = EMU_NAME;
    }

    TRect r(0, 0, width, height);
    r.move((tui_app->size.x - width) / 2, (tui_app->size.y - height) / 2);
    TDialog *d = new TDialog(r, header);

    for (int i = 0; i < nlines; i++)
        d->insert(new TStaticText(TRect(2, 2 + i, width - 2, 3 + i), lines[i]));

    int yn = (flags & MBX_QUESTION_YN) != 0;

    /* TButton::makeDefault() lets a focused non-default button grab the
       Enter key (cmDefault), and the group's focus order starts at the
       most recently inserted view. Select the intended button explicitly
       so Enter always activates it. */
    TButton *primary;
    if (yn) {
        primary = new TButton(TRect(width / 2 - 16, height - 4, width / 2 - 6, height - 2),
                              "~Y~es", cmYes, bfDefault);
        d->insert(primary);
        d->insert(new TButton(TRect(width / 2 + 6, height - 4, width / 2 + 16, height - 2),
                              "~N~o", cmNo, bfNormal));
    } else {
        primary = new TButton(TRect(width / 2 - 5, height - 4, width / 2 + 5, height - 2),
                              "O~K~", cmOK, bfDefault);
        d->insert(primary);
    }
    primary->select();

    ushort result = tui_app->deskTop->execView(d);
    TObject::destroy(d);

    if (yn)
        return (result == cmYes) ? 1 : 0;
    return 1;
}

int
ui_msgbox(int flags, char *message)
{
    return ui_msgbox_header(flags, NULL, message);
}

/* ---- VM picker ---------------------------------------------------------- */

/* Scan the VM manager path for virtual machines (subdirectories containing
   an 86box.cfg) and let the user pick one, mirroring the Qt frontend's VM
   manager for launches without a configuration file. Returns a strdup()'d
   path to the chosen 86box.cfg, or NULL when the user cancelled or no VM
   could be started (an explanatory message box is shown in that case). */
char *
tui_pick_vm(const char *vmpath)
{
    TStringCollection *coll;
    TDialog           *d;
    TListBox          *lb;
    DIR               *dir;
    struct dirent     *de;
    char               cfgpath[2048];
    char               full[2048];
    char              *result = NULL;
    int                count  = 0;
    int                width  = 46;
    int                height;

    if (!vmpath || !vmpath[0]) {
        tui_show_msgbox(MBX_ERROR, EMU_NAME,
                        "No configuration file was given, and no Virtual Machine path is configured.\n\n"
                        "Start " EMU_NAME " with a VM's 86box.cfg, or set the VMs path in the global "
                        "configuration file.");
        return NULL;
    }

    dir = opendir(vmpath);
    if (dir == NULL) {
        snprintf(full, sizeof(full),
                 "The configured Virtual Machine path could not be opened:\n\n%s", vmpath);
        tui_show_msgbox(MBX_ERROR, EMU_NAME, full);
        return NULL;
    }

    coll = new TStringCollection(16, 8);
    while ((de = readdir(dir)) != NULL) {
        struct stat st;

        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;
        path_append_filename(cfgpath, vmpath, de->d_name);
        path_append_filename(full, cfgpath, "86box.cfg");
        if (stat(full, &st) == 0 && S_ISREG(st.st_mode)) {
            coll->insert(newStr(de->d_name));
            count++;
        }
    }
    closedir(dir);

    if (count == 0) {
        TObject::destroy(coll);
        snprintf(full, sizeof(full),
                 "No virtual machines (directories containing 86box.cfg) were found in:\n\n%s", vmpath);
        tui_show_msgbox(MBX_ERROR, EMU_NAME, full);
        return NULL;
    }

    /* Rows: 1 frame + 2 padding + count list rows + 1 gap + 2 buttons
       + 1 frame. */
    height = (count < 8 ? count : 8) + 7;
    if (height > tui_app->size.y - 2)
        height = tui_app->size.y - 2;

    TRect r(0, 0, width, height);
    r.move((tui_app->size.x - width) / 2, (tui_app->size.y - height) / 2);
    d = new TDialog(r, "Select a Virtual Machine");

    TScrollBar *sb = new TScrollBar(TRect(width - 3, 3, width - 2, height - 4));
    lb               = new TListBox(TRect(3, 3, width - 3, height - 4), 1, sb);
    lb->newList(coll);
    d->insert(lb);
    d->insert(sb);

    d->insert(new TButton(TRect(3, height - 3, 14, height - 1), "O~K~", cmOK, bfDefault));
    d->insert(new TButton(TRect(17, height - 3, 28, height - 1), "~C~ancel", cmCancel, bfNormal));

    /* Focus the list (explicitly: the group's default focus order starts
       at the most recently inserted view). */
    lb->select();

    if (tui_app->deskTop->execView(d) != cmCancel) {
        int idx = lb->focused;
        if ((idx >= 0) && (idx < count)) {
            path_append_filename(cfgpath, vmpath, (const char *) coll->at(idx));
            path_append_filename(full, cfgpath, "86box.cfg");
            result = strdup(full);
        }
    }
    TObject::destroy(d);

    return result;
}

int
ui_msgbox_header(int flags, char *header, char *message)
{
    if (!tui_app_is_running()) {
        /* No terminal takeover yet: print to stderr. */
        fprintf(stderr, "\n%s: %s\n\n",
                header ? header : EMU_NAME, message ? message : "");
        return 1;
    }

    if (tui_on_ui_thread())
        return tui_show_msgbox(flags, header, message);

    return tui_queue_msgbox(flags, header, message);
}

/* snprintf returns the length it would have written, even when truncated. */
static void
ui_msg_append(char *message, size_t size, size_t *len, const char *format, ...)
{
    va_list args;
    int     written;

    if (*len >= size - 1)
        return;

    va_start(args, format);
    written = vsnprintf(message + *len, size - *len, format, args);
    va_end(args);

    if (written > 0)
        *len = ((size_t) written < size - *len) ? *len + written : size - 1;
}

int
ui_confirm_unsupported_hardware(const ui_unsupported_hardware_t *items, int count, int machine_missing)
{
    char   message[2048];
    size_t len = 0;

    if (count <= 0)
        return 1;

    message[0] = '\0';
    ui_msg_append(message, sizeof(message), &len, "%s\n\n", plat_get_string(STRING_UNSUPPORTED_TEXT));
    /* Show six names in full; for larger lists show five and the remainder. */
    const int listed = count <= 6 ? count : 5;
    for (int i = 0; i < listed; i++) {
        ui_msg_append(message, sizeof(message), &len, plat_get_string(items[i].kind), items[i].name);
        ui_msg_append(message, sizeof(message), &len, "\n");
    }
    if (count > listed) {
        ui_msg_append(message, sizeof(message), &len, plat_get_string(STRING_UNSUPPORTED_OTHERS), count - listed);
        ui_msg_append(message, sizeof(message), &len, "\n");
    }

    const int explanation = !machine_missing ? STRING_UNSUPPORTED_REMOVE :
                            count > 1 ? STRING_UNSUPPORTED_REPLACE_REMOVE : STRING_UNSUPPORTED_REPLACE;
    ui_msg_append(message, sizeof(message), &len, "\n%s\n\n%s", plat_get_string(explanation),
                  plat_get_string(STRING_UNSUPPORTED_CONTINUE));

    return ui_msgbox_header(MBX_WARNING | MBX_QUESTION_YN, plat_get_string(STRING_UNSUPPORTED_TITLE), message) == 1;
}

void
ui_progress_wait(const char *message, int total, int (*poll)(void *arg), void *arg)
{
    /* Called on the UI thread during long one-time device work (e.g. the
       Rage 128 GPU shader compile). The wait cannot be aborted, so simply
       show the progress in the status line and keep the terminal alive. */
    char msg[192];
    int  cur = poll(arg);
    while (cur < total) {
        snprintf(msg, sizeof(msg), "%s (%d/%d)", message, cur, total);
        tui_status_message(msg);
        plat_delay_ms(50);
        cur = poll(arg);
    }
    snprintf(msg, sizeof(msg), "%s (done)", message);
    tui_status_message(msg);
    tui_status_message("");
}

/* ---- Status bar plumbing ------------------------------------------------ */

void
ui_sb_update_icon(int tag, int active)
{
    tui_status_icon(tag, active);
}

void
ui_sb_update_icon_write(int tag, int write)
{
    tui_status_icon_write(tag, write);
}

void
ui_sb_update_icon_state(int tag, int state)
{
    tui_status_icon_state(tag, state);
}

void
ui_sb_update_icon_wp(int tag, int state)
{
    tui_status_icon_wp(tag, state);
}

void
ui_sb_update_tip(UNUSED(int arg))
{
    /* No tips in the TUI. */
}

void
ui_sb_update_panes(void)
{
    /* No-op. */
}

void
ui_sb_update_text(void)
{
    /* No-op. */
}

void
ui_sb_set_text(char *wstr)
{
    tui_status_message(wstr);
}

void
ui_sb_bugui(char *str)
{
    tui_status_bugui(str);
}

void
ui_sb_set_ready(int ready)
{
    tui_status_ready(ready);
}

void
ui_sb_mt32lcd(char *str)
{
    tui_status_mt32lcd(str);
}

void
ui_hard_reset_completed(void)
{
    tui_hard_reset_done();
}

void
ui_update_force_interpreter(void)
{
    /* No-op. */
}

/* ---- Emulator status ----------------------------------------------------- */

void
ui_emu_status(int speed_percent)
{
    /* Called from the emulator thread. */
    tui_status_speed(speed_percent);
}

void
ui_init_monitor(UNUSED(int monitor_index))
{
    /* No-op. */
}

void
ui_deinit_monitor(UNUSED(int monitor_index))
{
    /* No-op. */
}