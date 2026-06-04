#include "clipboard.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <array>
#include <string>
#include <sstream>

namespace a0::tui {

static bool hasCommand(const std::string& cmd) {
    std::string check = "command -v " + cmd + " >/dev/null 2>&1";
    return ::system(check.c_str()) == 0;
}

static void copyXclip(const std::string& text) {
    FILE* pipe = ::popen("xclip -selection clipboard -i", "w");
    if (pipe) {
        fwrite(text.data(), 1, text.size(), pipe);
        ::pclose(pipe);
    }
}

static void copyWlClipboard(const std::string& text) {
    FILE* pipe = ::popen("wl-copy", "w");
    if (pipe) {
        fwrite(text.data(), 1, text.size(), pipe);
        ::pclose(pipe);
    }
}

void copyToClipboard(const std::string& text) {
    if (text.empty()) return;

    // Try OSC 52 first (works in modern terminals without external tools)
    std::string b64;
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i = 0;
    while (i + 3 <= text.size()) {
        unsigned int v = (static_cast<unsigned char>(text[i]) << 16)
                       | (static_cast<unsigned char>(text[i+1]) << 8)
                       | static_cast<unsigned char>(text[i+2]);
        b64 += table[(v >> 18) & 0x3F];
        b64 += table[(v >> 12) & 0x3F];
        b64 += table[(v >> 6) & 0x3F];
        b64 += table[v & 0x3F];
        i += 3;
    }
    if (i < text.size()) {
        unsigned int v = static_cast<unsigned char>(text[i]) << 16;
        if (i + 1 < text.size()) v |= static_cast<unsigned char>(text[i+1]) << 8;
        b64 += table[(v >> 18) & 0x3F];
        b64 += table[(v >> 12) & 0x3F];
        if (i + 1 < text.size()) b64 += table[(v >> 6) & 0x3F];
        if (i + 1 < text.size()) b64 += '=';
        b64 += '=';
    }

    // Write OSC 52 sequence to stdout
    std::string osc52 = "\x1b]52;c;" + b64 + "\x07";
    fwrite(osc52.data(), 1, osc52.size(), stdout);
    fflush(stdout);

    // Fallback: xclip or wl-clipboard for terminals that don't support OSC 52
    const char* wayland = ::getenv("WAYLAND_DISPLAY");
    if (wayland && wayland[0]) {
        if (hasCommand("wl-copy"))
            copyWlClipboard(text);
    } else if (hasCommand("xclip")) {
        copyXclip(text);
    }
}

} // namespace a0::tui
