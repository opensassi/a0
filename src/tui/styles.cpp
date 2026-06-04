#include "styles.h"
#include "ftxui/dom/elements.hpp"

namespace a0::tui {

using namespace ftxui;

Decorator roleDecorator(MessageRole role) {
    switch (role) {
        case MessageRole::User:
            return bold | color(Color::Cyan);
        case MessageRole::Assistant:
            return color(Color::GreenLight);
        case MessageRole::Tool:
            return color(Color::BlueLight);
        case MessageRole::System:
            return dim | color(Color::Yellow);
        case MessageRole::Error:
            return bold | color(Color::RedLight);
    }
    return nothing;
}

std::string roleLabel(MessageRole role, const std::string& toolName) {
    switch (role) {
        case MessageRole::User:
            return "\u250C\u2500 You";
        case MessageRole::Assistant:
            return "\u250C\u2500 Assistant";
        case MessageRole::Tool:
            if (toolName.empty())
                return "\u250C\u2500 Tool";
            return "\u250C\u2500 Tool: " + toolName;
        case MessageRole::System:
            return "\u250C\u2500 System";
        case MessageRole::Error:
            return "\u250C\u2500 Error";
    }
    return "";
}

namespace style {
    auto compose = [](auto a, auto b) {
        return [a, b](ftxui::Element e) { return a(b(std::move(e))); };
    };
    const ElementDecorator codeBlock = compose(dim, border);
    const ElementDecorator inlineCode = inverted;
    const ElementDecorator heading1 = compose(bold, color(Color::White));
    const ElementDecorator heading2 = bold;
    const ElementDecorator dimmed = dim;
    const ElementDecorator link = compose(underlined, color(Color::Blue));
    const ElementDecorator toolOutput = nothing;
    const ElementDecorator toolOutputStderr = color(Color::Red);
    const ElementDecorator statusGood = color(Color::Green);
    const ElementDecorator statusBad = color(Color::Red);
}

} // namespace a0::tui
