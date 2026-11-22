// Qt Widgets desktop interface. Started by `reversa gui [FILE]`, `reversa --demo` or by launching
// the program without arguments (reversa-gui.exe on Windows).
#pragma once

namespace rv::cli {
struct Args;
}

namespace rv::gui {

int run(const cli::Args& args);
// False when no display is available (e.g. Linux over SSH without X11/Wayland).
bool display_available();

}  // namespace rv::gui
