/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM PC
 *          systems and compatibles from 1981 through fairly recent system
 *          designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Entry point and emulator thread for the Turbo Vision TUI
 *          frontend, ported from src/unix/sdl_main.c.
 *
 *
 *
 * Authors: Cacodemon345, David Hrdlička and 86Box contributors.
 *
 *          Copyright 2026.
 */

#include <stdbool.h>
#include <unistd.h>

#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdatomic.h>

/* The emulator core is C. Most 86Box headers wrap themselves in extern "C"
   when compiled as C++; the ones that do not are wrapped below. */
#include <86box/86box.h>
#include <86box/automation.h>
#include <86box/keyboard.h>
#include <86box/mouse.h>
#include <86box/plat.h>
#include <86box/thread.h>
#include <86box/timer.h>
#include <86box/tui.h>
#include <86box/ui.h>
#include <86box/video.h>

extern "C" {
#include <86box/gdbstub.h>
#include <86box/nvr.h>
#include <86box/sound.h>
#include <86box/version.h>
#include "cpu.h"
}

#include "tui_frontend.h"

/* Frontend globals the core references (see the SDL frontend's
   sdl_main.c for the original definitions). */
volatile int cpu_thread_run = 1;
bool         fast_forward   = false;
int          mouse_capture  = 0;
int          kbd_req_capture = 0;
int          rctrl_is_lalt  = 0;
int          update_icons   = 1;
int          hide_status_bar = 0;
int          hide_tool_bar  = 0;
int          fixed_size_x   = 640;
int          fixed_size_y   = 480;
volatile int tui_clean_mode = 0;

char           **main_argv      = nullptr;
volatile int     tui_exit_event = 0;

/* The TUI does not use the readline CLI monitor (src/unix/sdl_monitor.c);
   the TUI itself replaces it. */
extern "C" void
monitor_init(void)
{
}

extern "C" void
monitor_close(void)
{
}

/* Blit serialization (core calls these around target buffer access).
   Recursive, like the SDL3 mutexes the upstream shutdown flow was written
   against: do_stop() takes the blit lock and pc_close() takes it again on
   the same thread (and never releases it, by design). A non-recursive
   mutex deadlocks the UI thread there. */
static pthread_mutex_t blit_mtx;
static pthread_once_t  blit_mtx_once = PTHREAD_ONCE_INIT;

static void
blit_mutex_init(void)
{
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&blit_mtx, &attr);
    pthread_mutexattr_destroy(&attr);
}

void
startblit(void)
{
    pthread_once(&blit_mtx_once, blit_mutex_init);
    pthread_mutex_lock(&blit_mtx);
}

void
endblit(void)
{
    pthread_mutex_unlock(&blit_mtx);
}

/* ---- Emulator thread ----------------------------------------------------- */

/* The TUI renders the guest from tui_frame(), which runs before the blit
   data is queued, so there is no pixel renderer to present it. The core
   still expects someone to release the blit buffer once a frame is done:
   without this, the video card blocks in video_wait_for_buffer_monitor()
   after the very first frame and the emulator thread freezes. */
static void
tui_blit(UNUSED(int x), UNUSED(int y), UNUSED(int w), UNUSED(int h), int monitor_index)
{
    video_blit_complete_monitor(monitor_index);
}

static thread_t *thMain = nullptr;

static void
main_thread(UNUSED(void *param))
{
    uint32_t old_time;
    uint32_t new_time;
    uint32_t onesec_at;
    int      drawits;
    int      frames;

    is_cpu_thread = 1;

    framecountx = 0;
    old_time    = plat_get_ticks();
    onesec_at   = old_time;
    drawits = frames = 0;
    while (!is_quit && cpu_thread_run) {
        /* See if it is time to run a frame of code. */
        new_time = plat_get_ticks();

#ifdef USE_GDBSTUB
        if (gdbstub_next_asap && (drawits <= 0))
            drawits = 10;
        else
            drawits += (new_time - old_time);
#else
        drawits += (new_time - old_time);
#endif

        old_time = new_time;
        automation_poll();
        const int automation = automation_run_state();
        if ((drawits > 0 || fast_forward || (automation == AUTOMATION_RUN)) && !dopause && (automation != AUTOMATION_HOLD)) {
            /* Yes, so do one frame now. */
            drawits -= force_10ms ? 10 : 1;
            if (drawits > 50 || fast_forward || (automation == AUTOMATION_RUN))
                drawits = 0;

            /* Run a block of code. */
            pc_run();
            automation_quantum();

            /* Every 200 frames we save the machine status. */
            if (++frames >= (force_10ms ? 200 : 2000) && nvr_dosave) {
                nvr_save();
                nvr_dosave = 0;
                frames     = 0;
            }
        } else {
            /* Keep answering a GDB client while the machine isn't running. */
            gdbstub_poll();

            /* Just so we don't overload the host OS. */
            plat_delay_ms(1);
        }

        /* One-second statistics tick (the SDL frontend uses a timer
           thread for this). */
        if ((int32_t) (new_time - onesec_at) >= 1000) {
            onesec_at = new_time;
            pc_onesec();
        }

        /* If needed, handle a screen resize. The TUI renders the text
           screen, not pixels, so there is nothing to resize; just clear
           the flag the video core sets. */
        if (atomic_load(&doresize_monitors[0]) && !is_quit) {
            atomic_store(&doresize_monitors[0], 0);
        }
    }

    is_quit = 1;
}

void
do_start(void)
{
    /* We have not stopped yet. */
    is_quit = 0;

    /* Timer frequency for plat_timer_read(): nanoseconds. */
    timer_freq = 1000000000ULL;

    /* Start the emulator, really. */
    thMain = thread_create(main_thread, nullptr);
}

void
do_stop(void)
{
    if (!tui_on_ui_thread()) {
        tui_exit_event = 1;
        return;
    }

    startblit();
    is_quit = 1;

    pc_close(thMain);

    thMain = nullptr;
}

/* ---- Mouse (stubbed; guest mouse input is a deferred feature) ------------ */

void
plat_mouse_capture(UNUSED(int on))
{
    /* No host mouse capture in the TUI. */
}

/* ---- Frontend entry points ------------------------------------------------ */

int
plat_vidapi(UNUSED(const char *api))
{
    return 0;
}

void
plat_resize(UNUSED(int w), UNUSED(int h), UNUSED(int monitor_index))
{
    /* The TUI follows the terminal size, not the guest resolution. */
}

void
plat_resize_request(UNUSED(int w), UNUSED(int h), int monitor_index)
{
    atomic_store((&doresize_monitors[monitor_index]), 1);
}

/* Window title: the TUI has no window; keep the string for the status
   line and log. */
static char tui_title[512];

char *
ui_window_title(char *str)
{
    if (!str)
        return tui_title;
    memset(tui_title, 0, sizeof(tui_title));
    strncpy(tui_title, str, sizeof(tui_title) - 1);
    return str;
}

void
ui_window_title_real(void)
{
    /* No-op. */
}

/* ---- main ------------------------------------------------------------------ */

extern "C" int gfxcard[GFXCARD_MAX];

int
main(int argc, char **argv)
{
    int ret;

    main_argv = argv;

#ifndef _WIN32
    /* The TUI writes to terminals, sockets and pipes (tvision's own wakeup
       pipe, serial-over-socket, automation, SLiRP-forwarded connections).
       A peer closing early must not kill the process: writes fail with
       EPIPE instead. net_slirp.c does this locally for forwarded sockets,
       but the TUI needs it from the start (no VM is loaded yet when the
       VM picker runs). */
    signal(SIGPIPE, SIG_IGN);
#endif

    /* pclog() falls back to stdout when no --logfile was given. On the
       GUI frontends that is harmless: a GUI app has no visible stdout,
       and the official release builds compile pclog() out entirely. In
       a TUI, stdout *is* the display, and interleaved log lines corrupt
       it (they show up as garbage text and, e.g., "Illegal instruction"
       messages from the CPU core's logging). Pre-seed log_path so the
       lazy log open in pclog()/always_log()/fatal() lands on /dev/null
       instead of stdout, matching the silent behavior of mainline's GUI
       frontends. An explicit --logfile still wins, and this works in
       both terminal modes (tvision's /dev/tty and TVISION_USE_STDIO). */
    int have_log = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcasecmp(argv[i], "-L") || !strcasecmp(argv[i], "--logfile")) {
            have_log = 1;
            break;
        }
    }
    if (!have_log)
        strcpy(log_path, "/dev/null");

    ret = pc_init(argc, argv);
    if (ret == 0)
        return 0;

    /* Take over the terminal before anything can produce a message, so
       that ROM failures, config-load dialogs and unsupported-hardware
       warnings all appear as TUI dialogs. */
    ret = tui_app_boot();
    if (ret != 0)
        return ret;

    if (start_vmm) {
        /* No configuration file was given: like the Qt frontend's VM
           manager, let the user pick one of the VMs under the configured
           VMs path, then restart with that configuration. */
        char *cfg = tui_pick_vm(vmm_path);
        tui_app_shutdown();
        if (cfg == NULL)
            return 0;

        char **av = (char **) calloc((size_t) argc + 2, sizeof(char *));
        if (av != NULL) {
            for (int i = 0; i < argc; i++)
                av[i] = argv[i];
            av[argc] = cfg;
            execvp(argv[0], av);
        }
        fprintf(stderr, EMU_NAME ": could not restart with the selected VM.\n");
        return 1;
    }

    if (!pc_init_roms()) {
        tui_show_msgbox(MBX_ERROR | MBX_FATAL, EMU_NAME,
                        "86Box could not find any usable ROM images.\n\n"
                        "Please download a ROM set and extract it into the \"roms\" directory.");
        tui_app_shutdown();
        return 6;
    }

    pc_init_modules();

    /* Hand the video core our (present-less) blit completion hook. */
    video_setblit(tui_blit);

    for (uint8_t i = 1; i < GFXCARD_MAX; i++)
        gfxcard[i] = 0;

    monitor_init();

    /* Fire up the machine. */
    pc_reset_hard_init();

    /* Set the PAUSE mode. */
    plat_pause(0);

    /* Start the emulator thread. */
    do_start();

    /* Run the UI until the user quits. */
    ret = tui_app_run();

    /* Shut the emulator down. */
    do_stop();
    monitor_close();

    tui_app_shutdown();

    return ret;
}