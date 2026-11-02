/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM PC
 *          systems and compatibles from 1981 through fairly recent system
 *          designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          C-linkage stricmp()/strnicmp() for the TUI frontend.
 *
 *          The C parts of the emulator call these with C linkage. Turbo
 *          Vision also declares them (C++ linkage, noexcept) and defines
 *          them in its own library; both sets of symbols can coexist, but
 *          the C callers need these C-linkage definitions.
 *
 *
 *
 * Authors: The 86Box TUI frontend contributors.
 *
 *          Copyright 2026.
 */

#include <strings.h>

int
stricmp(const char *s1, const char *s2)
{
    return strcasecmp(s1, s2);
}

int
strnicmp(const char *s1, const char *s2, size_t n)
{
    return strncasecmp(s1, s2, n);
}