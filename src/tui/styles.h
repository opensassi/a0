#pragma once

#include <string>
#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"

namespace a0::tui {

enum class MessageRole {
    User,
    Assistant,
    Tool,
    System,
    Error
};

enum class ToolState {
    Pending,
    Running,
    Completed,
    Failed
};

enum class AgentState {
    Idle,
    Thinking,
    Executing,
    Error
};

ftxui::Decorator roleDecorator(MessageRole role);

std::string roleLabel(MessageRole role, const std::string& toolName = "");

namespace style {
    extern const ftxui::ElementDecorator codeBlock;
    extern const ftxui::ElementDecorator inlineCode;
    extern const ftxui::ElementDecorator heading1;
    extern const ftxui::ElementDecorator heading2;
    extern const ftxui::ElementDecorator dimmed;
    extern const ftxui::ElementDecorator link;
    extern const ftxui::ElementDecorator toolOutput;
    extern const ftxui::ElementDecorator toolOutputStderr;
    extern const ftxui::ElementDecorator statusGood;
    extern const ftxui::ElementDecorator statusBad;
}

constexpr int INPUT_PANEL_MIN_HEIGHT = 3;
constexpr int STATUS_BAR_HEIGHT = 1;
constexpr int SIDEBAR_WIDTH = 42;
constexpr int DIALOG_MIN_WIDTH = 60;
constexpr int DIALOG_MAX_WIDTH = 116;

} // namespace a0::tui
