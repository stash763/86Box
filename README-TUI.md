# 86Box TUI frontend

A console-only 86Box variant: no X11, no Wayland, no SDL, no Qt. The UI is a
Turbo Vision application (the fork at [`stash763/tvision`](https://github.com/stash763/tvision),
submodule `ext/tvision`) that renders through ncurses, so it runs on a raw
Linux console, in any terminal emulator, or inside a PTY driven by a test
harness. The guest's text-mode screen is rendered as **real terminal
character cells** — a host-side terminal-state engine (such as tui-test)
sees the guest screen exactly like a native full-screen terminal
application.

## Features

* **Guest text screen** — MDA/CGA/EGA/VGA text modes (chars, attributes,
  cursor) rendered continuously (~70 fps) as terminal cells inside a
  framed window.
* **Interactive operation** — machine control (Pause, Hard reset,
  Ctrl+Alt+Del, Fast forward, Power off, Exit with confirmation), media
  mount/eject for floppies and CD-ROMs, status line, message boxes.
* **Serial console windows** — a COM port attached to the `tui` device
  renders in an in-TUI VT/ANSI terminal window (libvterm).
* **VM picker** — launched without a configuration file, 86Box lists the
  virtual machines under the configured VMs path and restarts with the
  chosen one.
* **Clean mode** — hide all 86Box chrome so the guest screen covers the
  whole terminal.

## Building (Linux)

Requirements: CMake + Ninja, a C/C++ toolchain, `libncursesw-dev`, and the
git submodules (`ext/tvision`, `ext/libvterm`):

```sh
git submodule update --init --recursive
cmake --preset tui -B build/tui
cmake --build build/tui -j$(nproc)
```

The binary is `build/tui/src/86Box`. `TUI=ON` forces `QT=OFF`; SDL is not
compiled in.

If your distribution is missing new enough `libslirp`/`libsndfile`/`FAudio`
packages (as on this machine), point pkg-config at Homebrew's and disable
the optional synthesizers:

```sh
PKG_CONFIG_PATH=/home/linuxbrew/.linuxbrew/lib/pkgconfig \
cmake --preset tui -B build/tui \
    -D OPENAL=OFF -D RTMIDI=OFF -D FLUIDSYNTH=OFF \
    -D MUNT=OFF -D SOUNDCANVAS=OFF -D DISCORD=OFF
```

## Running

```sh
# Run a specific virtual machine:
86Box -R /path/to/roms "/path/to/VM/86box.cfg"

# No configuration file: shows the VM picker over the configured
# VMs path (see below), then boots the selection.
86Box -R /path/to/roms
```

Useful standard flags: `-R <dir>` ROM path, `-L <file>` log file,
`-N`/`--noconfirm` skip exit/power-off confirmations, `-E <dir>` override
the VMs path.

### Logging

Without `-L`, the emulator log (`pclog`) is **discarded** in the TUI. This
matches the other frontends in spirit: a GUI app's stdout is invisible, and
the official release builds compile `pclog()` out entirely — but in a TUI
stdout *is* the display, so letting log lines through would corrupt it
(they would appear as garbage text over the guest screen). Pass
`-L <file>` to get the usual 86Box log file, exactly like the Qt/SDL
frontends.

### Files and paths

Identical to the Qt frontend:

| Purpose | Path |
|---|---|
| Global configuration (`86box_global.cfg`, `vmm.ini`) | `~/.config/86Box/` |
| ROMs, assets, default VMs directory | `~/.local/share/86Box/` |
| Default VMs path (no `vmm_path` in the global config) | `~/.local/share/86Box/Virtual Machines/` |
| Per-VM data (nvr, images) | next to the VM's `86box.cfg` |

Portable mode (an `86box_global.cfg` next to the executable) behaves like
the other frontends: everything is looked up relative to the executable.

## Keyboard

| Key | Action |
|---|---|
| **F10** | Open the menu bar (Machine/Media/View/Help) |
| **Alt+F10** | Toggle clean mode (in clean mode, F10 goes to the guest) |
| Menu navigation | Arrows move, Enter opens/selects, letter hotkeys (e.g. `x` = E**x**it), Esc closes |
| Dialogs | Enter activates the highlighted/default button (`Yes`/`OK`), `y`/`n`/`o` hotkeys, Esc cancels |

Everything else — including F1–F12 (except F10), modifiers, and character
keys — is delivered to the guest as PC/XT scancodes.

## Serial console

Attach a COM port to the in-TUI terminal:

```ini
# In the VM's 86box.cfg:
serial1_device = tui
```

Then open **View → 1. COM1 console**. The window renders the port's output
through libvterm, and typing in the window feeds the port's input — usable
for a guest-side serial console or GETTY, and inspectable by terminal-state
tooling.

## Environment variables

| Variable | Effect |
|---|---|
| `TVISION_USE_STDIO=1` | Use stdin/stdout instead of `/dev/tty` — **required under PTY harnesses** (tui-test) and terminal multiplexers that don't own the controlling terminal |
| `TVISION_MAX_FPS` | Cap the TUI redraw rate |
| `TUI_NOCAPTURE=1` | Debug kill-switch: disable all guest memory reads (blank guest screen) — used to tell frontend bugs from emulator core bugs |

## Testing under a PTY

The TUI is designed to be driven headlessly: spawn it under a PTY with
`TERM` set (e.g. `xterm-256color`), a fixed window size (`TIOCSWINSZ`),
`TVISION_USE_STDIO=1` in the environment, and stderr redirected separately
if desired. Guest output, menus, and dialogs all render as ordinary
terminal cell updates; input is plain terminal key sequences (F10 = `ESC
[21~` on xterm). This is how the automated acceptance tests run.

## Limitations

* Graphics modes show a placeholder; only text modes render (half-block
  rendering is a possible follow-up).
* No mouse input to the guest yet; no sound-device configuration dialogs
  (configure via `86box.cfg` or another frontend).
* Linux only for now (tvision itself is cross-platform).