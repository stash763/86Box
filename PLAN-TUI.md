# 86Box TUI Frontend — Implementation Plan

A console-only 86Box variant: no X11, no Wayland, no SDL, no Qt. The UI is a
Turbo Vision application built on the fork at
[`stash763/tvision`](https://github.com/stash763/tvision) (submodule
`ext/tvision`), which renders through ncurses and therefore runs on a raw
Linux console, in any terminal emulator, or inside a PTY driven by a test
harness.

## Goals

1. **Text-mode output integration with tui-test** — the guest's text-mode
   screen (chars + attributes + cursor, any MDA/CGA/EGA/VGA/SVGA card) is
   rendered as *real terminal character cells* inside the TUI, so a host-side
   terminal-state engine (tui-test: control, inspect, test, record) sees the
   guest screen exactly like a native full-screen terminal application.
2. **Serial console integration with tui-test** — a COM port can be attached
   to an in-TUI VT/ANSI terminal window (libvterm), so serial-console sessions
   are likewise inspectable and drivable by tui-test.
3. **Fully interactive operation** — media mount/eject, machine control
   (pause/reset/CAD/power), status bar, message boxes, log viewer: a complete
   work session without leaving the terminal.

## Non-goals (documented, deferred)

* Mouse input to the guest (tvision mouse events → PS/2 mouse). Deferred.
* Graphics-mode rendering (placeholder message initially; half-block art is a
  possible follow-up).
* Windows/macOS support for the TUI build (tvision itself is cross-platform;
  the CMake guard is Linux-only for now).
* Full settings dialogs (machine/CPU/video/memory pickers). Configure via
  `86box.cfg`, another frontend, or the automation socket.

## Current status (updated 2026-11-03)

**Milestone reached: the emulated machine is directly drivable by
tui-test through the console passthrough (`--tui-clean` at 80×25): the
guest screen *is* the terminal, `submit "ver"` executes in the guest and
`MS-DOS Version 6.22` is asserted from terminal cells.**

* tui-test validation (the point of the passthrough): clean 80×25 spawn,
  boot to `C:\>`, `submit "ver"` → `expect "MS-DOS Version 6.22"` PASS,
  `submit "dir"` → `expect "COMMAND"` PASS, Alt+F10 chrome toggle PASS,
  F10 → Machine → Exit → confirm → clean exit (code 0, terminal
  restored). Screenshot evidence under `/tmp/opencode/vm-passthrough-*.png`.
* Clean-mode chrome fix (two tvision behaviors we must design around):
  1. tvision draws a group's **oldest subview last** — the desktop's
     `TBackground` painted over the guest view on every full group
     redraw. The background is now removed at construction (the guest
     screen always covers the desktop anyway).
  2. Views are clipped to their owner's rectangle, so a fullscreen guest
     view inside the standard desktop could never paint rows 0/24 — and
     `TProgram::setScreenMode` re-exposes *hidden* views
     (`TGroup::setState(sfExposed)` ignores `sfVisible`), letting hidden
     chrome draw again. In clean mode the desktop itself is now
     stretched over the whole terminal, so the guest paints over
     whatever the chrome draws — robust against the exposure quirk.
* Burst-typing fix: when several bytes arrive in one read, tvision
  reports the keys with `keyCode == 0` and the character only in
  `text[]` — `tui_key_translate()` fell back to nothing and whole
  bursts (a test harness typing a line at once) were silently dropped.
  It now falls back to `scan_from_char()` from `getText()`.
* Usage/log regression fix: the `/dev/null` `log_path` pre-seed moved
  from `main()` to `tui_app_boot()` — `pc_show_usage()` prints through
  `always_log()`, which the old pre-seed silenced (usage never appeared).
  Usage text and the log header now reach the terminal before tvision
  takes over (like a Qt frontend launched from a terminal); everything
  after boot is still silenced without `-L`.

Previous milestone (2026-11-02): full interactive sessions verified
against the real Test VM (MR BIOS → MS-DOS 6.22 from HDD), both launch
styles, under a PTY harness.

* Rendering fix: the guest screen and serial views were drawn through
  `TDrawBuffer::moveBuf()` with a `TScreenCell` array — but `moveBuf()`
  treats its source as a plain **char array**, so cell structs were read
  as text (attribute bytes and stack garbage appeared as CP437 glyphs).
  Both views now write per-cell with `putChar()`/`putAttribute()`.
* Logging fix: `pclog()` falls back to stdout when no `-L` is given; in a
  TUI stdout is the display, so log lines garbled it (visible e.g. as
  `[F000:...] Illegal instruction` fragments). The TUI now pre-seeds
  `log_path` to `/dev/null` when no `--logfile` is given — silent like
  the GUI frontends (whose stdout is invisible; official release builds
  compile `pclog()` out entirely), `-L` unchanged.

* Build: `cmake --preset tui` + `cmake --build build/tui` links clean
  (Linux, homebrew libslirp/libsndfile/FAudio via pkg-config); binary has
  no SDL/Qt/X11/Wayland DT_NEEDED entries.
* Paths: mainline parity — global config `~/.config/86Box/86box_global.cfg`,
  data `~/.local/share/86Box`, VMs path honored from the global config.
* Guest text screen: BIOS POST, memory count, MS-DOS boot and shell all
  render as terminal cells (~70 fps continuous); keyboard path verified
  end-to-end (typed `ver`/`dir` execute in the guest).
* Machine control menu works: F10 → dropdown → Exit → confirmation dialog
  → Enter = Yes → clean shutdown, exit code 0.
* No-argument launch shows the **VM picker** (subdirectories of the
  configured VMs path containing `86box.cfg`); Enter re-execs with the
  chosen configuration and boots it.

Notable fixes made along the way (all in the frontend unless noted):

* `tui_blit()` must call `video_blit_complete_monitor()` — with no pixel
  renderer the emulator thread otherwise blocks forever after frame 1.
* The blit mutex must be **recursive** (`do_stop()` and `pc_close()` both
  take it on the UI thread; SDL3 mutexes are recursive, pthread defaults
  are not) — non-recursive deadlocked shutdown.
* tvision focus order starts at the *last-inserted* view, and a focused
  non-default `TButton` **grabs** the Enter key (`makeDefault`): dialogs
  must `select()` their intended primary button explicitly.
* tvision's `SignalHandler` turned an inherited `SIG_IGN` for SIGPIPE into
  a fatal re-raise — fixed in the tvision fork (`sighandl.cpp` now honors
  `SIG_IGN`); the TUI also installs `SIG_IGN` for SIGPIPE at startup.
* Before `execvp()` (VM picker restart) the tvision console must be
  suspended (`TApplication::suspend()`): tvision replaces stderr with a
  close-on-exec capture pipe, and the new image would inherit a
  reader-less pipe on fd 2 (the dynamic loader then dies of SIGPIPE on
  its first stderr write).
* `TUI_NOCAPTURE=1` env kill-switch (kept, documented) disables all guest
  memory reads to tell frontend bugs from core bugs.

## Architecture

### Threading model (mirrors the proven SDL frontend)

```
main thread (UI)                        emulator thread ("main_thread")
─────────────────────────               ──────────────────────────────
main()                                  while (!is_quit && cpu_thread_run)
  pc_init(argc, argv)                     paced pc_run() loop, 10 ms quanta
  pc_init_roms()                          automation_poll() (if enabled)
  pc_init_modules()                       nvr_save cadence (200/2000 frames)
  start emulator thread ──────────────►   doresize handling
  TApplication::run()                     text screen snapshot on each blit
    tvision event loop                    serial ring buffers (char_tui)
    TEventQueue::waitForEvents()          ─── TEventQueue::wakeUp() ────►
    views redraw from snapshots
```

* Guest memory is **only** touched on the emulator thread (the blit path in
  `src/video/video.c`, next to `automation_frame()`), never from the UI
  thread. Snapshots are double-buffered; the UI thread reads the front
  buffer. A torn frame is at most one refresh old — irrelevant.
* The emulator thread wakes the UI thread with tvision's thread-safe
  `TEventQueue::wakeUp()`; views also run a ~30 Hz blink/refresh timer.

### Frontend selection

```
cmake --preset regular -D QT=OFF -D TUI=ON     →  86Box (TUI, links tvision)
cmake --preset regular                          →  86Box (Qt)      [unchanged]
cmake --preset regular -D QT=OFF                →  86Box (SDL)     [unchanged]
```

`src/CMakeLists.txt` picks the frontend:

```cmake
if(QT)
    add_subdirectory(qt)
elseif(TUI)
    add_compile_definitions(USE_TUI_UI)
    add_subdirectory(tui)
else()
    add_compile_definitions(USE_SDL_UI)
    add_subdirectory(unix)
endif()
```

`TUI=ON` forces `QT=OFF`. The TUI build must link **no** X11/Wayland/SDL/Qt
libraries (verify with `ldd`).

### New files

| File | Purpose |
|---|---|
| `src/tui/CMakeLists.txt` | `plat` + `ui` object libraries, tvision + libvterm wiring |
| `src/tui/vterm/CMakeLists.txt` | libvterm build wrapper (perl table generation, as tvterm does) |
| `src/tui/tui_main.cpp` | `main()`, emulator thread, `do_start`/`do_stop`, `startblit`/`endblit`, global state (ported from `src/unix/sdl_main.c`) |
| `src/tui/tui_plat.cpp` | `plat_*` generic: files, strings, paths, timers via `clock_gettime`, `plat_pause`, `plat_power_off` (ported from `src/unix/sdl_plat.c`) |
| `src/tui/tui_plat_unix.cpp` | `plat_*` unix: XDG dirs, rom/asset paths, mmap, thread names, block devices, `plat_run_command` (ported from `src/unix/sdl_plat_unix.c`) |
| `src/tui/tui_joystick.cpp` | No-op `joystick_init/close/process/get_axis` (core calls them; no SDL) |
| `src/tui/tui_app.hpp/.cpp` | `TUIApp` (TApplication): menu bar, status line, desktop, window management, clean mode, `ui_sb_*`, title handling |
| `src/tui/tui_screen.hpp/.cpp` | `GuestScreenView`: draws text screen snapshot, keyboard → `keyboard_input()` |
| `src/tui/tui_keymap.cpp` | tvision key code → XT set-1 scancode table + modifier synthesis |
| `src/tui/tui_serial.hpp/.cpp` | `SerialConsoleView`: libvterm terminal widget (tvterm pattern) |
| `src/tui/tui_dialogs.cpp` | `ui_msgbox*`, `ui_confirm_unsupported_hardware`, `ui_progress_wait` as tvision dialogs |
| `src/tui/tui_menu.cpp` | Menu construction + actions: media, machine, view, help |
| `src/tui/text_capture.c` | BDA + VRAM text screen snapshot (C, emulator thread), double buffer |
| `src/include/86box/tui.h` | C interface: `tui_frame()` hook macro, snapshot API, serial glue |
| `src/char/char_tui.c` | Serial backend `tui` (`serial1_device = tui`), modeled on `char_automation.c` |

### Reused files (unchanged, referenced from `src/tui/CMakeLists.txt`)

* `src/unix/unix_thread.c`, `unix_dynld.c`, `unix_netsocket.c`
* `src/unix/linux_cdrom_ioctl.c` + `dummy_floppy_ioctl.c` (Linux)
* `src/unix/sdl_cdrom.c` — despite the name this is the SDL-free media
  mount/eject implementation (`floppy_mount`, `cdrom_mount`,
  `plat_cdrom_ui_update`, …) used by the SDL frontend; the TUI links it too.

### Modified files

| File | Change |
|---|---|
| `CMakeLists.txt` | `option(TUI …)`, force `QT=OFF` when `TUI=ON`, `add_subdirectory(ext/tvision)` + `ext/libvterm` wrapper when `TUI`, Windows console subsystem condition extended to TUI |
| `src/CMakeLists.txt` | frontend selection (above) |
| `src/video/video.c` | call `tui_frame(x, y, w, h, monitor)` next to `automation_frame()` in `video_blit_memtoscreen_monitor()` |
| `src/char/char.c` | register `char_tui_com_device` in `char_devices[]` |
| `src/char/CMakeLists.txt` | build `char_tui.c` when `TUI` |
| `CMakePresets.json` | `tui` preset (`QT=OFF`, `TUI=ON`) |
| `.github/workflows/*` | CI job for the TUI build (phase 4) |

## Phase 0 — Build plumbing + runnable skeleton

**Tasks**

1. Root `CMakeLists.txt`: `TUI` option; force QT off; add tvision/libvterm
   subdirectories when `TUI=ON` (with `TV_BUILD_EXAMPLES=OFF`).
2. `src/tui/CMakeLists.txt` producing `plat` and `ui` object libraries;
   reuse unix sources; link `tvision` (+ `vterm` in phase 2).
3. `tui_plat.cpp` / `tui_plat_unix.cpp` / `tui_joystick.cpp`: full `plat_*`
   surface (see inventory below). Timers: `clock_gettime(CLOCK_MONOTONIC)`.
   `plat_get_exe_name`: `/proc/self/exe`. Pref dir: `$XDG_DATA_HOME/86Box`
   or `$HOME/.local/share/86Box` (same logic the SDL build uses, minus SDL).
4. `tui_main.cpp`: `main()` sequence copied from `sdl_main.c:main()`:
   `pc_init` → `pc_init_roms` (fatal → dialog/exit 6) → `pc_init_modules`
   → `monitor_init` → `pc_reset_hard_init()` → `plat_pause(0)` → start
   emulator thread → `TUIApp::run()` → shutdown (`do_stop`, `monitor_close`).
   Emulator thread loop ported verbatim from `main_thread()` in
   `sdl_main.c` (drawits pacing, automation poll, nvr save, doresize).
5. `tui_app.cpp`: minimal TApplication — menu bar (Action/Help stubs),
   status line, desktop; `ui_sb_*` no-ops; `ui_msgbox` via tvision
   `msgBox()` fallback to stderr if app not running yet.
6. `startblit`/`endblit`: mutex-based (core calls these around buffer
   access; SDL uses a mutex — same here).

**`plat_*`/`ui_*` inventory to implement** (from `plat.h`, `ui.h` and SDL
frontend usage): `plat_getcwd/chdir/fopen/fopen64/remove/tempfile`,
`plat_get_exe_name`, `plat_get_global_config_dir`, `plat_get_global_data_dir`,
`plat_get_temp_dir`, `plat_get_vmm_dir`, `plat_init_rom_paths`,
`plat_init_asset_paths`, `plat_dir_check/file_check/dir_create`,
`plat_mmap/munmap`, `plat_timer_read`, `plat_get_ticks`, `plat_delay_ms`,
`plat_pause`, `plat_fast_forward`, `plat_mouse_capture`, `plat_vidapi`,
`plat_vidapi_name`, `plat_resize`, `plat_resize_request`,
`plat_language_code(_r)`, `plat_get_cpu_string`, `plat_set_thread_name`,
`plat_break`, `plat_run_command`, `plat_clean_up`, `plat_lock_volumes`,
`plat_unlock_volumes`, `plat_get_string`, `plat_power_off`,
`plat_cdrom_ui_update` (reuse sdl_cdrom.c), `plat_is_block_device`,
`plat_get_block_device_size`, `path_*` helpers, `stricmp/strnicmp`,
`ui_msgbox(_header)`, `ui_confirm_unsupported_hardware`,
`ui_progress_wait`, `ui_emu_status`, `ui_hard_reset_completed`,
`ui_init_monitor`, `ui_deinit_monitor`, all `ui_sb_*`,
`ui_update_force_interpreter`, `ui_window_title(_real)`,
`startblit`, `endblit`, `joystick_init/close/process/get_axis`,
`monitor_init/close` (TUI: no-op / log), `do_start`, `do_stop`.

**Acceptance criteria**

* `cmake --preset regular -D QT=OFF -D TUI=ON` configures and builds.
* `ldd build/.../86Box` shows no libSDL, libQt, libX11, libwayland.
* Running in a terminal shows the tvision chrome; quitting from the menu
  exits cleanly (exit code 0), emulator thread joins.
* Without ROMs: fatal dialog appears in the TUI (or stderr when headless).

## Phase 1 — Guest text screen (tui-test integration #1)

**Text capture** (`src/tui/text_capture.c`, C, emulator thread only)

Refactor of the proven logic in `src/automation.c` (`peek_phys`,
`text_info`):

* BDA: mode `0x449` (≤3 or 7 = text), cols `0x44A`, rows `0x484+1`
  (clamped 25–60 / 40–132), cursor `0x450+2*page`, page base
  `0x44E`; chars/attrs at `(mode==7 ? 0xB0000 : 0xB8000) + pageoff`.
* Reads via `mem_readb_phys()` with `cycles` saved/restured (device
  wait states must not be charged — see automation.c comment).
* `tui_frame()` called from `video_blit_memtoscreen_monitor()` (next to
  `automation_frame`) captures into a back buffer, flips an atomic index,
  bumps a frame counter. No-op macro when not building the TUI.
* Snapshot: `{ valid, mode, cols, rows, cur_row, cur_col, frame,
  uint8_t chars[60][132], uint8_t attrs[60][132] }` ×2 (double buffer).

**GuestScreenView** (`tui_screen.cpp`)

* `draw()`: for each row, build a `TDrawBuffer` of CP437 bytes with
  `TColorAttr(attr)` (BIOS attribute constructor — exact 16-color match);
  blink attribute (`0x80`) → `slBlink` style (or strip when guest blink
  phase off — blink phase driven by a tvision timer, ~530 ms).
  `writeBuf()` the row. Center the screen in the view; overscan ignored.
* Hardware cursor: drawn as a solid block/underline on the captured
  position while cursor visible (blink via timer). Also `setCursor()` on
  the view so tui-test sees a terminal cursor position.
* Graphics mode (`text_info` invalid): centered placeholder
  `"Graphics mode W×H — not rendered in TUI"`.
* `handleEvent()`: when focused, all `evKeyDown` → `tui_key_translate()`
  → `keyboard_input(down, scan)`; consumed. **F10 is reserved** (opens the
  menu bar, tvision convention) — everything else reaches the guest.
  Modifier-only state changes (Ctrl/Alt/Shift/AltGr) are synthesized as
  make/break from `controlKeyState` diffs so guests that poll shift state
  work. On focus loss: `keyboard_all_up()`.

**Keymap** (`tui_keymap.cpp`)

tvision `keyCode` is `(scan << 8) | char` in Borland convention, so the
physical XT scan code is `keyCode >> 8` for most keys. Table:

* E0-prefixed (gray) keys — `Home/Up/PgUp/Left/Right/End/Down/PgDn/Ins/Del`
  → `0x100 | scan` (same encoding `keyboard_input()` expects: SDL uses
  `0x147`…`0x153`); gray `* - +` → normal codes; gray `/`, gray Enter →
  `0x135`, `0x11C` where distinguishable; RCtrl `0x11D`, RAlt `0x138` when
  the terminal reports them.
* F11/F12 → `0x57/0x58`; Ctrl/Alt/Shift combos keep the base scan;
  `kbShiftTab` → `0x0F`; `kbAltX` style codes (scan in high byte) work
  naturally.
* Fallbacks for `scan == 0` (Unicode input): translate `charCode` via a
  small CP437→scan table for the cases terminals report without scan.

**tui-test operation**

* tvision performs terminal I/O via `/dev/tty` by default; under a harness
  set `TVISION_USE_STDIO=1` (documented; the launcher also honors
  `--tui-stdio`). tvision buffers stderr so log noise never corrupts the
  screen.
* Clean mode (`--tui-clean` CLI flag, also a View-menu toggle and
  `Alt+F10`-free): hides menu bar, status line and window frame; the guest
  screen is drawn at terminal origin. This is the mode tui-test asserts
  against: cells, colors, cursor are the guest's own.
* `--vmpath` and all stock 86Box CLI options work as usual.

**Acceptance criteria**

* Boot DOS with any VGA/SVGA card: the DOS prompt renders as terminal
  text with correct colors and cursor; typing works incl. arrows, F-keys,
  Ctrl/Alt combos; DOS `EDIT`/`EDIT.COM` full-screen TUI works.
* Under tui-test (PTY): screen content is readable by the harness,
  keystrokes injected by the harness reach the guest.
* MDA/Hercules machine (mode 7) renders as well.
* Guest app switching to graphics mode shows the placeholder; switching
  back to text restores the view automatically.

## Phase 2 — Serial console window (tui-test integration #2)

**Backend** (`src/char/char_tui.c`, modeled on `char_automation.c`)

* Registered as `char_tui_com_device` ("TUI console", internal name
  `tui`), listed in `char_devices[]` → `serial1_device = tui` in
  `86box.cfg`.
* `char_attach(0, read, write, status, control, port_config, dev)`;
  port number parsed from the port name (`"COMn"`).
* `write()` (emulator thread): bytes → per-port TX ring (lock-free,
  single-producer/single-consumer) → `TEventQueue::wakeUp()`.
* `read()`: drains the per-port RX ring (filled by the view); paced like
  char_automation does *not* need to be — the UART polls; return 1 byte per
  call when available (the UART's own pacing applies).
* `status()`: CTS/DSR/DCD on, RI off (constant "null modem"); `control()`
  records guest DTR/RTS/break for display.
* `port_config()`: nothing (view displays baud from `char_port->com`).

**SerialConsoleView** (`tui_serial.cpp`)

* libvterm (`VTerm` + `VTermScreen`) receives guest TX bytes on the UI
  thread (drained from the TX ring in a timer callback and on wake-up);
  `vterm_screen_flush_damage` → redraw damaged cells via `TDrawBuffer`
  (BIOS/RGB attrs from `VTermScreenCell`; tvterm is the reference).
* Keyboard: printable text from `keyDown.getText()` (UTF-8) → RX ring;
  control keys mapped to bytes (Enter `\r`, Backspace `\x7f`/`\x08`,
  Ctrl+letter, arrows → CSI sequences, PgUp/PgDn → application mode aware).
* Scrollback ring (tvterm-style), window title `COMn — 115200 8N1`.
* Multiple instances: one window per COM port configured with the `tui`
  backend; View menu lists them; `Alt+1..4` switches focus.
* Complementary existing option (documented, no new code): 
  `serial1_device = stdio` with PTY mode lets an external tool attach to
  the COM port directly.

**Acceptance criteria**

* `serial1_device = tui`, guest DOS `CTTY COM1:` or a Linux serial console:
  fully interactive session in the window with correct VT handling.
* tui-test can read what the guest printed on serial and type into it.
* Hard reset does not crash the views (backend close/reopen handled).

## Phase 3 — Interactive operation

**Menus** (`tui_menu.cpp`)

* **Machine**: Pause (toggle, shows state), Hard reset, Ctrl+Alt+Del
  (`keyboard_input` 3-code sequence), Fast forward (toggle), Power off
  (soft, `plat_power_off()`), Exit.
* **Media**: Floppy A/B…: Mount… (tvision `TFileDialog`, filters
  `*.img;*.ima;*.360;*.720;*.1.2;*.1.44;*.2.88;*.86f;…`), Eject, and
  write-protect toggle; CD-ROM: Mount image (`*.iso;*.cue;*.mds;…`),
  Eject; ZIP/MO/tape/cartridge when configured (reuse `sdl_cdrom.c`
  functions: `floppy_mount`, `cdrom_mount`, `mo_mount`, …).
* **View**: Guest screen, serial windows, Log viewer, Clean mode toggle.
* **Help**: About (version, license).
* **Status line**: speed % (`ui_emu_status`), activity icons
  (`ui_sb_update_icon*` mapped to letters: `A:` `B:` `CD` `HDD` `NET`
  `SND`), message text (`ui_sb_set_text`), MT-32 LCD (`ui_sb_mt32lcd`).

**Log viewer** — tail of the emulator log (`pc_log` buffer or a sink
registered via `automation_log_text`-style hook; simplest: capture through
`log_*` via a frontend-side ring populated from the standard log callback)
in a scrollable `TTerminal`/`TListViewer` window.

**Acceptance criteria**

* Full session without leaving the TUI: boot DOS, mount a floppy image,
  run a program, eject, hard reset, power off.
* Status bar shows drive activity and speed.
* `ui_msgbox`-driven warnings (e.g. bad image) appear as dialogs and can
  be dismissed with the keyboard.

## Phase 4 — Polish, docs, CI

* `README-TUI.md`: build instructions, feature guide, keymap table,
  tui-test recipes (PTY spawn, clean mode, serial window), config
  reference (`serial1_device = tui`), env vars (`TVISION_USE_STDIO`,
  `TVISION_MAX_FPS`, `ESCDELAY`).
* `CMakePresets.json` `tui` preset; GitHub Actions workflow job
  (Ubuntu: `libncursesw-dev`, `libgpm-dev`, `perl`).
* Fatal-error path: dialog before exit; works with stderr redirected.
* `--tui-no-confirm` (skip exit/power-off confirmations) for scripted runs.

## Key risks / mitigations

| Risk | Mitigation |
|---|---|
| BDA staleness (direct-CRTC apps) | Capture falls back to "graphics mode" placeholder; renderer-hook capture is the documented future fallback |
| tvision `/dev/tty` default bypasses a harness PTY | `TVISION_USE_STDIO=1` / `--tui-stdio`; tested in phase 1 |
| Keymap holes (grey keys, AltGr, F11/F12 on odd terminals) | tvision parses xterm/fixterms/kitty protocols; table covers set-1; test matrix in phase 1 |
| Cross-thread guest memory reads | All reads on emulator thread (blit hook); UI reads snapshots only |
| Blink timing fidelity | Guest blink phase approximated with a tvision timer (530 ms); hardware cursor blink likewise |
| tvision fork divergence | Pinned submodule commit; upgrade deliberately |

## Build & test quickstart

```sh
git submodule update --init --recursive
cmake --preset regular -D QT=OFF -D TUI=ON -D CMAKE_BUILD_TYPE=Release
cmake --build build/regular -j$(nproc)
# smoke test (no ROMs needed — expect the fatal dialog):
build/regular/src/86Box
# with a VM:
build/regular/src/86Box -P /path/to/vm
# under a PTY for harness-driven runs:
TVISION_USE_STDIO=1 build/regular/src/86Box -P /path/to/vm --tui-clean
```