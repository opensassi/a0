#pragma once

#include <string>

namespace a0::tui {

/// Copy text to the system clipboard.
/// Uses OSC 52 escape sequence (works in Kitty, iTerm2, WezTerm, tmux).
/// Falls back to `xclip` on X11 or `wl-clipboard` on Wayland.
void copyToClipboard(const std::string& text);

} // namespace a0::tui
