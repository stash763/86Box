#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM PC
#          systems and compatibles from 1981 through fairly recent system
#          designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          CMake build script for the Turbo Vision TUI frontend.
#
# Authors: The 86Box TUI frontend contributors.
#
#          Copyright 2026.
#

add_library(plat OBJECT
    tui_main.cpp
    tui_plat.cpp
    tui_joystick.cpp
)

target_sources(plat PRIVATE
    tui_plat_unix.cpp
    ../unix/unix_netsocket.c
    ../unix/unix_dynld.c
)

if (NOT CPPTHREADS)
    set(THREADS_PREFER_PTHREAD_FLAG TRUE)
    find_package(Threads REQUIRED)
    target_link_libraries(86Box Threads::Threads)
    target_sources(plat PRIVATE ../unix/unix_thread.c)
endif()

add_library(ui OBJECT
    tui_app.cpp
    tui_screen.cpp
    tui_keymap.cpp
    tui_serial.cpp
    tui_dialogs.cpp
    tui_menu.cpp
    text_capture.c
    tui_strings.c
    ../unix/sdl_cdrom.c
)

# Turbo Vision (ext/tvision) and libvterm (built through src/tui/vterm)
# are added from the top-level CMakeLists when TUI=ON.
target_link_libraries(ui PUBLIC tvision vterm)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(plat PRIVATE ../unix/linux_cdrom_ioctl.c)
    target_sources(plat PRIVATE ../unix/dummy_floppy_ioctl.c)
else()
    target_sources(plat PRIVATE ../unix/dummy_cdrom_ioctl.c)
    target_sources(plat PRIVATE ../unix/dummy_floppy_ioctl.c)
endif()

target_compile_definitions(plat PUBLIC _FILE_OFFSET_BITS=64)
target_link_libraries(plat ${CMAKE_DL_LIBS})