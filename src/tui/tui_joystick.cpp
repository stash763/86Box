/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM PC
 *          systems and compatibles from 1981 through fairly recent system
 *          designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Joystick stubs for the TUI frontend. The core calls these on
 *          every frame; host joysticks are a possible future feature.
 *
 *
 *
 * Authors: The 86Box TUI frontend contributors.
 *
 *          Copyright 2026.
 */

#include <stdint.h>

/* These 86Box headers wrap themselves in extern "C" for C++. */
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/gameport.h>

int joysticks_present = 0;

/* Joystick state arrays the gameport core reads. No host joysticks are
   supported by the TUI, so they stay zeroed. */
joystick_state_t      joystick_state[GAMEPORT_MAX][MAX_JOYSTICKS];
plat_joystick_state_t plat_joystick_state[MAX_PLAT_JOYSTICKS];

void
joystick_init(void)
{
    joysticks_present = 0;
}

void
joystick_close(void)
{
}

void
joystick_process(UNUSED(uint8_t gp))
{
}