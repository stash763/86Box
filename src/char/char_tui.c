/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM PC
 *          systems and compatibles from 1981 through fairly recent system
 *          designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Serial port backend for the TUI frontend's console windows.
 *
 *          What the guest sends is buffered for the TUI's VT/ANSI terminal
 *          view, and what is typed there is handed to the guest at the
 *          UART's own rate, in emulated time. Modeled on char_automation.c.
 *
 * Authors: The 86Box TUI frontend contributors.
 *
 *          Copyright 2026.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/char.h>
#include <86box/plat.h>
#include <86box/timer.h>
#include <86box/tui.h>
#include "cpu.h"

#ifdef USE_TUI_UI

typedef struct {
    int          port; /* 1 = COM1 */
    char_port_t *char_port;
    uint64_t     in_tsc; /* TSC of the last byte handed to the guest */
} char_tui_t;

/* The UART polls for input once per bit time, so that a byte waiting is
   taken as soon as the receiver is free; bytes typed (or pasted) into the
   console window would then arrive up to ten times faster than the line
   could carry them, and a guest that reads its UART on interrupts at line
   rate overruns. A character is given out no faster than its frame takes
   on the line the guest has set up: start, data, parity, stop. Same pacing
   scheme as char_automation.c. */
static size_t
char_tui_read(uint8_t *buf, size_t len, void *priv)
{
    char_tui_t *dev     = (char_tui_t *) priv;
    double      byte_us = 0.0;

    if (dev->char_port->com.baud) {
        int bits = 1 + dev->char_port->com.data_bits + (dev->char_port->com.parity & 1) + dev->char_port->com.stop_bits;
        byte_us  = (bits * 1000000.0) / dev->char_port->com.baud;
    }
    if (len && byte_us > 0.0) {
        /* TIMER_USEC is CPU cycles per microsecond, in 32.32 fixed point. */
        uint64_t gap = (uint64_t) (byte_us * ((double) TIMER_USEC / 4294967296.0));
        if ((tsc >= dev->in_tsc) && ((tsc - dev->in_tsc) < gap))
            return 0;
    }
    if (len && tui_serial_input(dev->port, buf, 1))
        return 1;
    return 0;
}

static size_t
char_tui_write(uint8_t *buf, size_t len, void *priv)
{
    char_tui_t *dev = (char_tui_t *) priv;

    tui_serial_output(dev->port, buf, len);
    return len;
}

static uint32_t
char_tui_status(void *priv)
{
    char_tui_t *dev = (char_tui_t *) priv;

    /* Null-modem: the console window is always ready. */
    return tui_serial_lines(dev->port);
}

static void
char_tui_control(uint32_t flags, void *priv)
{
    char_tui_t *dev = (char_tui_t *) priv;

    tui_serial_control(dev->port, flags);
}

static void
char_tui_close(void *priv)
{
    char_tui_t *dev = (char_tui_t *) priv;

    tui_serial_attach(dev->port, 0);
    free(dev);
}

static void *
char_tui_init(UNUSED(const device_t *info))
{
    char_tui_t *dev = (char_tui_t *) calloc(1, sizeof(char_tui_t));

    dev->char_port = char_attach(0, char_tui_read, char_tui_write, char_tui_status,
                                 char_tui_control, NULL, dev);
    /* serial_devices_init() names the port "COMn". */
    dev->port = atoi(dev->char_port->name + 3);
    tui_serial_attach(dev->port, 1);

    return dev;
}

const device_t char_tui_com_device = {
    .name          = "TUI console",
    .internal_name = "tui",
    .flags         = DEVICE_COM | DEVICE_HOTPLUG,
    .local         = 0,
    .init          = char_tui_init,
    .close         = char_tui_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

#endif /* USE_TUI_UI */