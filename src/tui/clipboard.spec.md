# Clipboard Spec

## 1. Overview

Copies text to the system clipboard with a three-tier fallback strategy. First attempts the **OSC 52** escape sequence (supported by Kitty, iTerm2, WezTerm, tmux) by writing a base64-encoded payload to stdout. If `WAYLAND_DISPLAY` is set, falls back to `wl-copy` (wl-clipboard). Otherwise falls back to `xclip` on X11. The OSC 52 emission runs unconditionally so that terminal multiplexers and SSH sessions that support it can capture the clipboard even when no desktop clipboard tool is installed.

**Source files:** `src/tui/clipboard.h/.cpp`

**Dependencies:** Standard C (`cstdio`, `cstdlib`), `xclip` (optional), `wl-copy` (optional)

## 2. Component Specifications

```cpp
namespace a0::tui {

/// Copy text to the system clipboard.
/// Uses OSC 52 escape sequence (works in Kitty, iTerm2, WezTerm, tmux).
/// Falls back to `xclip` on X11 or `wl-clipboard` on Wayland.
/// No-op when text is empty.
void copyToClipboard(const std::string& text);

} // namespace a0::tui
```

### Fallback logic

```
copyToClipboard(text)
  ├─ text.empty() → return
  ├─ base64-encode text
  ├─ write OSC 52 sequence to stdout: ESC ] 52 ; c ; <base64> BEL
  ├─ if WAYLAND_DISPLAY is set and wl-copy exists:
  │    └─ pipe text → wl-copy
  └─ else if xclip exists:
       └─ pipe text → xclip -selection clipboard -i
```

### Base64 encoding

Custom inline implementation without external library dependency. Standard table `A-Za-z0-9+/` with `=` padding for the final group.

## 3. Architecture Diagram

```mermaid
graph TB
    subgraph Input
        TEXT[copyToClipboard(text)]
    end

    subgraph Routing
        EMPTY{text.empty?}
        B64[Base64 encode]
        OSC52[Write OSC 52 to stdout]
        WAYLAND{WAYLAND_DISPLAY set?}
        HAS_WL{wl-copy exists?}
        HAS_XCLIP{xclip exists?}
    end

    subgraph Outputs
        OSC_OUT[Terminal handles OSC 52]
        WL_OUT[wl-copy pipes to Wayland]
        XCLIP_OUT[xclip pipes to X11]
    end

    TEXT --> EMPTY
    EMPTY -->|yes| DONE[return]
    EMPTY -->|no| B64
    B64 --> OSC52
    OSC52 --> WAYLAND
    WAYLAND -->|yes| HAS_WL
    WAYLAND -->|no| HAS_XCLIP
    HAS_WL -->|yes| WL_OUT
    HAS_WL -->|no| DONE
    HAS_XCLIP -->|yes| XCLIP_OUT
    HAS_XCLIP -->|no| DONE
```

## 4. Testing Requirements

| Test | Verification |
|------|-------------|
| Empty text | No output, no process spawned |
| Text copied via OSC 52 | OSC 52 sequence written to stdout |
| Base64 output correctness | Decoded string matches input |
| Wayland fallback | `wl-copy` invoked when `WAYLAND_DISPLAY` is set |
| X11 fallback | `xclip` invoked when no Wayland |
| No external tools | Only OSC 52 emitted, no crash |
| Large text (multi-KB) | Completes without truncation |
| Special characters | UTF-8 and binary safe via base64 |
