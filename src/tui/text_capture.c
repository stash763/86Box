/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM PC
 *          systems and compatibles from 1981 through fairly recent system
 *          designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Text-mode screen capture for the TUI frontend.
 *
 *          Reads the BIOS data area and the text plane through the normal
 *          physical memory map, exactly like the automation socket's `text`
 *          command does, and keeps double-buffered snapshots for the UI
 *          thread. All guest memory access happens on the emulator thread
 *          (through the tui_frame() blit hook); the UI thread only ever
 *          copies out the front buffer under the same mutex.
 *
 *
 *
 * Authors: The 86Box TUI frontend contributors.
 *
 *          Copyright 2026.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/mem.h>
#include <86box/plat.h>
#include <86box/thread.h>
#include <86box/tui.h>
#include "cpu.h"

/* Read guest memory without charging the emulated CPU for it: device
   handlers bill the CPU for each access (VGA charges its wait states), and
   we read up to ~8 KB per frame. Same trick as automation.c's peek_phys(). */
static uint8_t
peek_phys(uint32_t addr)
{
    int     saved = cycles;
    uint8_t val   = mem_readb_phys(addr);

    cycles = saved;
    return val;
}

static tui_text_snap_t snaps[2];
static int             snap_front = 0; /* index the UI thread reads */
static uint64_t        frame_no   = 0;
static mutex_t        *snap_mtx   = NULL;

/* Called from tui_frame() on the emulator thread. */
static void
capture(void)
{
    int  back  = 1 - snap_front;
    int  mode, cols, rows, page, cur_col, cur_row, valid;
    uint32_t base;

    mode  = peek_phys(0x449) & 0x7f;
    valid = (mode <= 3) || (mode == 7);

    cols = peek_phys(0x44a) | (peek_phys(0x44b) << 8);
    rows = peek_phys(0x484) + 1;
    if ((cols < 40) || (cols > TUI_TEXT_MAX_COLS))
        cols = 80;
    if ((rows < 25) || (rows > TUI_TEXT_MAX_ROWS))
        rows = 25;

    page    = peek_phys(0x462) & 7;
    cur_col = peek_phys(0x450 + (page * 2));
    cur_row = peek_phys(0x451 + (page * 2));
    base    = ((mode == 7) ? 0xb0000 : 0xb8000) + (peek_phys(0x44e) | (peek_phys(0x44f) << 8));

    tui_text_snap_t *s = &snaps[back];
    s->mode    = mode;
    s->cols    = cols;
    s->rows    = rows;
    s->cur_row = cur_row;
    s->cur_col = cur_col;
    s->frame   = ++frame_no;

    if (valid) {
        for (int r = 0; r < rows; r++) {
            uint32_t rowbase = base + (((uint32_t) r * cols) * 2);
            for (int c = 0; c < cols; c++) {
                s->chars[r][c] = peek_phys(rowbase + (c * 2));
                s->attrs[r][c] = peek_phys(rowbase + (c * 2) + 1);
            }
        }
    }
    s->valid = valid;

    thread_wait_mutex(snap_mtx);
    /* Flip: the UI thread now reads `back`; our next capture overwrites the
       old front buffer. */
    snap_front = back;
    thread_release_mutex(snap_mtx);
}

void
tui_frame(UNUSED(int x), UNUSED(int y), UNUSED(int w), UNUSED(int h), UNUSED(int monitor_index))
{
    /* Only monitor 0's text screen is rendered in the TUI. */
    if (monitor_index != 0)
        return;

    /* Debug kill-switch: TUI_NOCAPTURE=1 disables all guest memory reads,
       to tell frontend bugs apart from core bugs. */
    static int no_capture = -1;
    if (no_capture < 0)
        no_capture = getenv("TUI_NOCAPTURE") != NULL;
    if (no_capture)
        return;

    if (!snap_mtx)
        snap_mtx = thread_create_mutex();

    capture();
}

void
tui_text_snapshot(tui_text_snap_t *snap)
{
    if (!snap_mtx) {
        memset(snap, 0, sizeof(tui_text_snap_t));
        return;
    }

    thread_wait_mutex(snap_mtx);
    memcpy(snap, &snaps[snap_front], sizeof(tui_text_snap_t));
    thread_release_mutex(snap_mtx);
}