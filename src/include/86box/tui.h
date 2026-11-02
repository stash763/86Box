/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM PC
 *          systems and compatibles from 1981 through fairly recent system
 *          designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the Turbo Vision text-based user interface.
 *
 *
 *
 * Authors: The 86Box TUI frontend contributors.
 *
 *          Copyright 2026.
 */
#ifndef EMU_TUI_H
#define EMU_TUI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum text screen size we can snapshot (matches the automation `text`
   command's clamping of the BIOS data area values). */
#define TUI_TEXT_MAX_COLS 132
#define TUI_TEXT_MAX_ROWS 60

/* One snapshot of the guest's text-mode screen, captured on the emulator
   thread by the blit hook (tui_frame) and read by the UI thread. */
typedef struct tui_text_snap {
    int      valid;    /* 1 if the guest is in a text mode with a sane BDA */
    int      mode;     /* BIOS video mode number */
    int      cols;
    int      rows;
    int      cur_row;  /* hardware cursor position, */
    int      cur_col;  /* 0-based */
    uint64_t frame;    /* snapshot serial number */
    uint8_t  chars[TUI_TEXT_MAX_ROWS][TUI_TEXT_MAX_COLS]; /* CP437 */
    uint8_t  attrs[TUI_TEXT_MAX_ROWS][TUI_TEXT_MAX_COLS]; /* BIOS attributes */
} tui_text_snap_t;

#ifdef USE_TUI_UI

/* Set by pc_init() from the --tui-clean command line parameter: hide the
   86Box chrome so the guest screen covers the whole terminal. */
extern volatile int tui_clean_mode;

/* Called by the video core (emulator thread) whenever a frame is handed to
   the renderer. Captures the text screen if the guest is in a text mode. */
extern void tui_frame(int x, int y, int w, int h, int monitor_index);

/* Copy the most recent snapshot (front buffer) into *snap. Thread-safe. */
extern void tui_text_snapshot(tui_text_snap_t *snap);

/* Serial console glue between the `tui` char device backend (char_tui.c,
   emulator thread) and the TUI serial views (UI thread). Port is 1 for COM1. */

/* Guest transmitted bytes (emulator thread). Never blocks. */
extern void tui_serial_output(int port, const uint8_t *buf, size_t len);
/* Bytes typed in the view, waiting for the guest (emulator thread). Returns
   how many bytes were copied into buf (0 if none). */
extern size_t tui_serial_input(int port, uint8_t *buf, size_t len);
/* Modem status lines the guest sees (CTS/DSR/DCD/RI, CHAR_COM_* bits). */
extern uint32_t tui_serial_lines(int port);
/* Guest changed DTR/RTS/break (CHAR_COM_* bits). */
extern void tui_serial_control(int port, uint32_t flags);
/* The backend was attached (1) or detached/closed (0) by a reset. */
extern void tui_serial_attach(int port, int attached);

#else

#define tui_frame(x, y, w, h, monitor_index)

#endif /* USE_TUI_UI */

#ifdef __cplusplus
}
#endif

#endif /*EMU_TUI_H*/