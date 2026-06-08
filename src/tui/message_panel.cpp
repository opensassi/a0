#include "message_panel.h"
#include "ftxui/component/component.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include <algorithm>
#include <sstream>

namespace a0::tui {

class MessagePanel::Impl {
public:
    std::vector<MessageEntry> entries;
    ftxui::Component renderer;
    int focusIndex = 0;
    bool autoScroll = true;
};

MessagePanel::MessagePanel()
    : m_impl(std::make_unique<Impl>()) {
    m_impl->renderer = ftxui::Renderer([this] {
        int total = static_cast<int>(m_impl->entries.size());

        if (total == 0) {
            return ftxui::vbox({
                ftxui::text("") | ftxui::size(ftxui::HEIGHT, ftxui::GREATER_THAN, 1)
            });
        }

        // Determine which entry to focus for yframe viewport positioning
        int focusIdx;
        if (m_impl->autoScroll) {
            focusIdx = total - 1;
        } else {
            focusIdx = std::min(m_impl->focusIndex, total - 1);
        }

        ftxui::Elements elems;

        // ↑ hint at top of vbox (may be clipped by yframe when focused below)
        if (focusIdx > 0) {
            elems.push_back(
                ftxui::text("\u2191 " + std::to_string(focusIdx) + " more") |
                ftxui::dim | ftxui::bold
            );
        }

        for (int i = 0; i < total; ++i) {
            auto entry = xRenderEntry(m_impl->entries[i]);
            if (i == focusIdx) {
                entry = entry | ftxui::focus;
            }
            elems.push_back(std::move(entry));
        }

        // ↓ hint at bottom of vbox (may be clipped by yframe when focused above)
        int below = total - 1 - focusIdx;
        if (below > 0) {
            elems.push_back(
                ftxui::text("\u2193 " + std::to_string(below) + " more") |
                ftxui::dim | ftxui::bold
            );
        }

        return ftxui::vbox(std::move(elems)) | ftxui::yframe | ftxui::vscroll_indicator;
    });
}

MessagePanel::~MessagePanel() = default;

ftxui::Component MessagePanel::component() const {
    return m_impl->renderer;
}

int MessagePanel::append(const MessageEntry& entry) {
    m_impl->entries.push_back(entry);
    return static_cast<int>(m_impl->entries.size()) - 1;
}

int MessagePanel::beginStreaming(MessageRole role) {
    MessageEntry entry;
    entry.role = role;
    entry.streaming = true;
    m_impl->entries.push_back(entry);
    return static_cast<int>(m_impl->entries.size()) - 1;
}

int MessagePanel::streamUpdate(int index, const std::string& text) {
    if (index < 0 || index >= static_cast<int>(m_impl->entries.size())) return -1;
    m_impl->entries[index].content = text;
    return 0;
}

int MessagePanel::endStream(int index) {
    if (index < 0 || index >= static_cast<int>(m_impl->entries.size())) return -1;
    m_impl->entries[index].streaming = false;
    return 0;
}

int MessagePanel::appendToolCall(const std::string& name,
                                  ToolState state,
                                  const std::string& output) {
    MessageEntry entry;
    entry.role = MessageRole::Tool;
    entry.toolName = name;
    entry.toolState = state;
    entry.toolOutput = output;
    entry.collapsed = false;
    m_impl->entries.push_back(entry);
    return static_cast<int>(m_impl->entries.size()) - 1;
}

int MessagePanel::updateToolCall(int index, ToolState state, const std::string& output) {
    if (index < 0 || index >= static_cast<int>(m_impl->entries.size())) return -1;
    m_impl->entries[index].toolState = state;
    if (!output.empty()) {
        m_impl->entries[index].toolOutput = output;
    }
    return 0;
}

int MessagePanel::updateToolCall(const std::string& name, ToolState state, const std::string& output) {
    for (int i = static_cast<int>(m_impl->entries.size()) - 1; i >= 0; --i) {
        auto& e = m_impl->entries[i];
        if (e.role == MessageRole::Tool && e.toolName == name && e.toolState == ToolState::Running) {
            e.toolState = state;
            if (!output.empty()) e.toolOutput = output;
            return i;
        }
    }
    return -1;
}

void MessagePanel::setToolCallArgs(int index, const std::string& args) {
    if (index >= 0 && index < static_cast<int>(m_impl->entries.size())) {
        m_impl->entries[index].toolArgs = args;
    }
}

void MessagePanel::clear() {
    m_impl->entries.clear();
    m_impl->focusIndex = 0;
    m_impl->autoScroll = true;
}

void MessagePanel::scrollToBottom() {
    m_impl->autoScroll = true;
}

void MessagePanel::scrollUp(int n) {
    int total = static_cast<int>(m_impl->entries.size());
    int current = m_impl->autoScroll && total > 0 ? total - 1 : m_impl->focusIndex;
    m_impl->autoScroll = false;
    m_impl->focusIndex = std::max(0, current - n);
}

void MessagePanel::scrollDown(int n) {
    int total = static_cast<int>(m_impl->entries.size());
    int current = m_impl->autoScroll && total > 0 ? total - 1 : m_impl->focusIndex;
    int last = total > 0 ? total - 1 : 0;
    m_impl->focusIndex = std::min(last, current + n);
    if (total > 0 && m_impl->focusIndex >= last) {
        m_impl->autoScroll = true;
    }
}

void MessagePanel::scrollToTop() {
    m_impl->autoScroll = false;
    m_impl->focusIndex = 0;
}

int MessagePanel::scrollTop() const {
    return m_impl->focusIndex;
}

bool MessagePanel::isAtBottom() const {
    if (m_impl->entries.empty()) return true;
    return m_impl->autoScroll || m_impl->focusIndex >= static_cast<int>(m_impl->entries.size()) - 1;
}

int MessagePanel::loadHistory(const std::vector<::a0::mpsc::SessionMessage>& messages) {
    for (const auto& pm : messages) {
        MessageEntry entry;
        if (pm.role == "user") entry.role = MessageRole::User;
        else if (pm.role == "assistant") entry.role = MessageRole::Assistant;
        else if (pm.role == "tool") entry.role = MessageRole::Tool;
        else if (pm.role == "system") entry.role = MessageRole::System;
        else entry.role = MessageRole::Error;
        entry.content = pm.content;
        entry.toolName = pm.name;
        entry.timestamp = pm.createdAt;
        m_impl->entries.push_back(entry);
    }
    return static_cast<int>(messages.size());
}

size_t MessagePanel::count() const {
    return m_impl->entries.size();
}

ftxui::Element MessagePanel::xRenderEntry(const MessageEntry& entry) const {
    ftxui::Element content;
    auto role = entry.role;

    if (role == MessageRole::Tool && !entry.toolName.empty()) {
        content = xRenderToolBlock(entry);
    } else if (entry.streaming) {
        content = xRenderStreamingPlaceholder(entry);
    } else {
        content = ftxui::paragraph(entry.content);
    }

    std::string label;
    if (role == MessageRole::Tool && !entry.toolName.empty()) {
        label = "\u250C\u2500 Tool: " + entry.toolName;
        if (!entry.toolArgs.empty()) {
            label += " " + entry.toolArgs;
        }
    } else {
        label = roleLabel(role, entry.toolName);
    }
    auto header = ftxui::text(label) | roleDecorator(role);

    ftxui::Elements elems;
    elems.push_back(header);
    if (content) elems.push_back(content);
    elems.push_back(ftxui::text(""));
    return ftxui::vbox(std::move(elems));
}

ftxui::Element MessagePanel::xRenderToolBlock(const MessageEntry& entry) const {
    std::string stateStr;
    ftxui::Color stateColor;
    switch (entry.toolState) {
        case ToolState::Pending:
            stateStr = "\u23F3 pending";
            stateColor = ftxui::Color::Yellow;
            break;
        case ToolState::Running:
            stateStr = "\u23F3 running";
            stateColor = ftxui::Color::Blue;
            break;
        case ToolState::Completed:
            stateStr = "\u2705 completed";
            stateColor = ftxui::Color::Green;
            break;
        case ToolState::Failed:
            stateStr = "\u274C failed";
            stateColor = ftxui::Color::Red;
            break;
    }

    ftxui::Elements headerElems;
    headerElems.push_back(ftxui::text("\U0001F527 " + entry.toolName) | ftxui::bold);
    headerElems.push_back(ftxui::text("  "));
    headerElems.push_back(ftxui::text(stateStr) | ftxui::color(stateColor));
    auto header = ftxui::hbox(std::move(headerElems));

    ftxui::Elements body;
    body.push_back(header);
    if (!entry.toolOutput.empty()) {
        body.push_back(ftxui::paragraph(entry.toolOutput) | ftxui::color(ftxui::Color::GrayDark));
    }

    return ftxui::vbox(std::move(body)) | ftxui::color(ftxui::Color::BlueLight);
}

ftxui::Element MessagePanel::xRenderStreamingPlaceholder(const MessageEntry& entry) const {
    auto cursor = ftxui::text("\u258C") | ftxui::blink;
    ftxui::Elements elems;
    elems.push_back(ftxui::paragraph(entry.content));
    elems.push_back(cursor);
    return ftxui::hbox(std::move(elems));
}

ftxui::Element MessagePanel::xRenderCollapsedToggle(const MessageEntry& entry) const {
    if (entry.collapsed) {
        return ftxui::text("[\u25B6 " + entry.toolName + "]");
    }
    return ftxui::emptyElement();
}

} // namespace a0::tui
