/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM PC
 *          systems and compatibles from 1981 through fairly recent system
 *          designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Menu construction and command execution for the TUI frontend.
 *
 *          Note on keyboard shortcuts: menu items deliberately carry no
 *          Alt+letter hotkeys, so every Alt combination reaches the guest
 *          (DOS applications use them heavily). The menu bar is opened
 *          with F10; Alt+F10 toggles clean mode.
 *
 *
 *
 * Authors: The 86Box TUI frontend contributors.
 *
 *          Copyright 2026.
 */

#define Uses_TFileDialog
#include <tvision/tv.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Turbo Vision (included above) declares stricmp()/strnicmp() itself, so
   plat.h's declarations must be kept out of this translation unit. */
#define PLAT_NO_STRICMP_DECL

/* The emulator core is C. Most 86Box headers wrap themselves in extern "C"
   when compiled as C++; the ones that do not are wrapped below. */
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/keyboard.h>
#include <86box/fdd.h>
#include <86box/cdrom.h>
#include <86box/plat.h>
#include <86box/ui.h>

extern "C" {
#include <86box/version.h>
}

#include "tui_frontend.h"
#include "tui_app.hpp"

/* Media mount/eject live in the shared sdl_cdrom.c. */
extern "C" {
void floppy_mount(uint8_t id, char *fn, uint8_t wp);
void floppy_eject(uint8_t id);
void cdrom_mount(uint8_t id, char *fn);
void cdrom_eject(uint8_t id);
}

/* How many floppy/CD drives to offer. */
extern "C" {
#include <86box/device.h>
}

/* Run a tvision file open dialog. Returns true and fills path on success. */
static bool
tui_file_open_dialog(const char *wildcard, const char *title, char *path, size_t path_len)
{
    TFileDialog *d = new TFileDialog(wildcard, title,
                                     "~N~ame", fdOpenButton, 100);
    TRect r(0, 0, 76, 18);
    r.move((tui_app->size.x - 76) / 2, (tui_app->size.y - 18) / 2);
    d->locate(r);

    ushort result = tui_app->deskTop->execView(d);
    bool ok = false;
    if (result != cmCancel && result != 0) {
        char buf[1024];
        d->getFileName(buf);
        if (buf[0]) {
            snprintf(path, path_len, "%s", buf);
            ok = true;
        }
    }
    TObject::destroy(d);
    return ok;
}

/* ---- Actions ------------------------------------------------------------ */

void
tui_action_pause(void)
{
    plat_pause(dopause ^ 1);
}

void
tui_action_hard_reset(void)
{
    pc_reset_hard();
}

void
tui_action_cad(void)
{
    /* Ctrl+Alt+Del, set-1 make codes. */
    keyboard_input(1, 0x1d); /* left Ctrl */
    keyboard_input(1, 0x38); /* left Alt */
    keyboard_input(1, 0x53); /* Del (E0 53) */
    keyboard_input(0, 0x53);
    keyboard_input(0, 0x38);
    keyboard_input(0, 0x1d);
}

void
tui_action_fast_forward(void)
{
    plat_fast_forward(!fast_forward);
}

void
tui_action_power_off(void)
{
    /* Same condition the Qt frontend uses: ask unless suppressed by the
       configuration or -N/--noconfirm. */
    if (confirm_exit && confirm_exit_cmdl) {
        int r = tui_show_msgbox(MBX_QUESTION_YN, EMU_NAME,
                                "Power off the machine?");
        if (!r)
            return;
    }
    plat_power_off();
    tui_exit_event = 1;
}

void
tui_action_exit(void)
{
    if (confirm_exit && confirm_exit_cmdl) {
        int r = tui_show_msgbox(MBX_QUESTION_YN, EMU_NAME,
                                "Exit 86Box? The machine will be powered off.");
        if (!r)
            return;
    }
    tui_exit_event = 1;
}

/* ---- Menu construction --------------------------------------------------- */

static TSubMenu &
menu_machine(void)
{
    return *new TSubMenu("~M~achine", kbNoKey) +
        *new TMenuItem("~P~ause", cmPause, kbNoKey) +
        *new TMenuItem("Hard ~r~eset", cmHardReset, kbNoKey) +
        *new TMenuItem("Ctrl+Alt+~D~el", cmCAD, kbNoKey) +
        *new TMenuItem("~F~ast forward", cmFastForward, kbNoKey) +
          newLine() +
        *new TMenuItem("Power ~o~ff", cmPowerOff, kbNoKey) +
        *new TMenuItem("E~x~it", cmExitApp, kbNoKey);
}

static TSubMenu &
menu_media(void)
{
    return *new TSubMenu("~M~edia", kbNoKey) +
        *new TMenuItem("Floppy A: mount...", cmFloppyMount + 0, kbNoKey) +
        *new TMenuItem("Floppy A: eject", cmFloppyEject + 0, kbNoKey) +
        *new TMenuItem("Floppy B: mount...", cmFloppyMount + 1, kbNoKey) +
        *new TMenuItem("Floppy B: eject", cmFloppyEject + 1, kbNoKey) +
          newLine() +
        *new TMenuItem("CD-ROM 0 mount...", cmCDROMMount + 0, kbNoKey) +
        *new TMenuItem("CD-ROM 0 eject", cdCDROMEject + 0, kbNoKey) +
        *new TMenuItem("CD-ROM 1 mount...", cmCDROMMount + 1, kbNoKey) +
        *new TMenuItem("CD-ROM 1 eject", cdCDROMEject + 1, kbNoKey);
}

static TSubMenu &
menu_view(void)
{
    return *new TSubMenu("~V~iew", kbNoKey) +
        *new TMenuItem("~C~lean mode (Alt+F10)", cmToggleCleanMode, kbNoKey) +
        *new TMenuItem("~1~. COM1 console", cmSerialWindow + 1, kbNoKey) +
        *new TMenuItem("~2~. COM2 console", cmSerialWindow + 2, kbNoKey) +
        *new TMenuItem("~3~. COM3 console", cmSerialWindow + 3, kbNoKey) +
        *new TMenuItem("~4~. COM4 console", cmSerialWindow + 4, kbNoKey);
}

static TSubMenu &
menu_help(void)
{
    return *new TSubMenu("~H~elp", kbNoKey) +
        *new TMenuItem("~A~bout", cmAbout, kbNoKey);
}

TSubMenu *
tui_menu_build(void)
{
    /* Each submenu is fully built before being chained, because the +
    operator appends items to the last submenu in the chain. */
    TSubMenu &root = menu_machine();
    root + menu_media();
    root + menu_view();
    root + menu_help();
    return &root;
}

/* ---- Command execution ---------------------------------------------------- */

void
tui_menu_exec_command(int command)
{
    int base = 0;

    if (command >= cmFloppyMount && command < cmFloppyMount + 8) {
        char path[1024];
        if (tui_file_open_dialog("*.img;*.ima;*.360;*.720;*.1.2;*.1.44;*.2.88;*.86f;*.d88;*.d77;*.xdf;*.fdi;*.imd;*.td0;*.imd", "Floppy image", path, sizeof(path)))
            floppy_mount(command - cmFloppyMount, path, 0);
        return;
    }
    if (command >= cmFloppyEject && command < cmFloppyEject + 8) {
        floppy_eject(command - cmFloppyEject);
        return;
    }
    if (command >= cmFloppyWP && command < cmFloppyWP + 8) {
        return; /* not offered yet */
    }
    if (command >= cmCDROMMount && command < cmCDROMMount + 8) {
        char path[1024];
        if (tui_file_open_dialog("*.iso;*.cue;*.mds;*.mdx;*.chd;*.bin;*.aaru;*.MDS", "CD-ROM image", path, sizeof(path)))
            cdrom_mount(command - cmCDROMMount, path);
        return;
    }
    if (command >= cdCDROMEject && command < cdCDROMEject + 8) {
        cdrom_eject(command - cdCDROMEject);
        return;
    }
    if (command >= cmSerialWindow && command < cmSerialWindow + TUI_SERIAL_PORTS + 1) {
        tui_serial_window_open(command - cmSerialWindow);
        return;
    }

    switch (command) {
        case cmPause:
            tui_action_pause();
            break;
        case cmHardReset:
            tui_action_hard_reset();
            break;
        case cmCAD:
            tui_action_cad();
            break;
        case cmFastForward:
            tui_action_fast_forward();
            break;
        case cmPowerOff:
            tui_action_power_off();
            break;
        case cmAbout: {
            char about[1024];
            snprintf(about, sizeof(about),
                     "%s v%s\n\nA hypervisor and IBM PC system emulator.\n\n"
                     "TUI frontend - Turbo Vision on ncurses.\n\n"
                     "Released under the GNU General Public License version 2 or later.",
                     EMU_NAME, EMU_VERSION_FULL);
            tui_show_msgbox(0, "About", about);
            break;
        }
        default:
            (void) base;
            break;
    }
}