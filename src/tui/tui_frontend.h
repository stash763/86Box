/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM PC
 *          systems and compatibles from 1981 through fairly recent system
 *          designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Internal glue for the Turbo Vision TUI frontend. Not installed;
 *          only used by the files in src/tui.
 *
 *
 *
 * Authors: The 86Box TUI frontend contributors.
 *
 *          Copyright 2026.
 */

#ifndef TUI_FRONTEND_H
#define TUI_FRONTEND_H

#include <stdbool.h>
#include <stdint.h>

#include <86box/86box.h>
#include <86box/tui.h>

/* Maximum number of serial ports with a TUI console window. */
#define TUI_SERIAL_PORTS 4

/* ---- tui_main.cpp ---------------------------------------------------- */

/* sound.h only declares this for C (#ifdef bool hides it from C++).
   Defined in tui_main.cpp, read by the sound core, set through
   plat_fast_forward(). */
#ifdef __cplusplus
extern "C" bool fast_forward;
#endif

/* argv as passed to main(), for plat_get_exe_name's fallback. */
extern char **main_argv;

/* Set when the UI wants the emulator to stop (mirrors SDL's exit_event). */
extern volatile int tui_exit_event;

void do_start(void);
void do_stop(void);

/* ---- tui_app.cpp ----------------------------------------------------- */

/* Construct the TApplication (takes over the terminal). Returns 0 on
   success. Must be called once, from the main thread. */
int  tui_app_boot(void);
/* TApplication::run(). Returns the process exit code. */
int  tui_app_run(void);
/* Destroy the application, restoring the terminal. */
void tui_app_shutdown(void);
/* Thread-safe: make the UI event loop run one iteration now. */
void tui_app_wake(void);
bool tui_app_is_running(void);
bool tui_on_ui_thread(void);

/* UI-thread only: show a modal message box. Returns 1 for Yes, 0 for No
   (MBX_QUESTION_YN), 1 otherwise. */
int tui_show_msgbox(int flags, const char *header, const char *message);

/* Any thread except the UI thread: queue a message box for the UI thread
   and block until it is dismissed. */
int tui_queue_msgbox(int flags, const char *header, const char *message);

/* UI-thread only: show a modal VM picker over the VM manager path (mirrors
   the Qt frontend's VM manager for launches without a configuration file).
   Returns a strdup()'d 86box.cfg path, or NULL when nothing was picked. */
char *tui_pick_vm(const char *vmpath);

/* Thread-safe status updates; applied by the UI thread. */
void tui_status_paused(int paused);
void tui_status_speed(int percent);
void tui_status_message(const char *msg);   /* ui_sb_set_text */
void tui_status_bugui(const char *str);     /* ui_sb_bugui */
void tui_status_mt32lcd(const char *str);   /* ui_sb_mt32lcd */
void tui_status_icon(int tag, int active);  /* ui_sb_update_icon */
void tui_status_icon_write(int tag, int write);
void tui_status_icon_state(int tag, int state);
void tui_status_icon_wp(int tag, int state);
void tui_status_ready(int ready);
void tui_hard_reset_done(void);

/* Clean mode: hide the menu bar and status line, draw the guest screen
   over the whole terminal (for harness-driven runs). */
void tui_set_clean_mode(int on);
int  tui_get_clean_mode(void);

/* Machine actions (tui_menu.cpp). */
void tui_action_pause(void);
void tui_action_hard_reset(void);
void tui_action_cad(void);
void tui_action_fast_forward(void);
void tui_action_power_off(void);
void tui_action_exit(void);

/* ---- tui_screen.cpp --------------------------------------------------- */

/* Refresh the guest screen view from the latest text snapshot. Called on
   the UI thread (from idle). */
void tui_screen_update(void);
/* Keyboard focus helpers. */
void tui_screen_focus(void);

/* ---- tui_serial.cpp --------------------------------------------------- */

/* Drain the serial rings into the views. Called on the UI thread. */
void tui_serial_update(void);
/* Open (or reopen) the console window for COM port (1-based). */
void tui_serial_window_open(int port);
void tui_serial_window_close(int port);

/* Implemented in tui_serial.cpp, called from char_tui.c (emulator
   thread) through the tui.h C interface:
   tui_serial_output, tui_serial_input, tui_serial_lines,
   tui_serial_control, tui_serial_attach */

#endif /* TUI_FRONTEND_H */