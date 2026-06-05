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
    ftxui::Component scrollContainer;
    ftxui::Component renderer;
    int m_scrollTop = 0;
    bool autoScroll = true;

    void ensureScroll() {
        if (autoScroll && !entries.empty()) {
            int total = static_cast<int>(entries.size());
            m_scrollTop = std::max(0, total - VISIBLE_ENTRIES);
        }
    }
};

static int xWindowStart(int scrollTop, int total, int window) {
    return std::max(0, std::min(scrollTop, std::max(0, total - window)));
}

MessagePanel::MessagePanel()
    : m_impl(std::make_unique<Impl>()) {
    m_impl->renderer = ftxui::Renderer([this] {
        if (m_impl->entries.empty()) {
            ftxui::Elements emptyElems;
            emptyElems.push_back(ftxui::text("") | ftxui::size(ftxui::HEIGHT, ftxui::GREATER_THAN, 1));
            return ftxui::vbox(std::move(emptyElems));
        }

        int total = static_cast<int>(m_impl->entries.size());
        int start = xWindowStart(m_impl->m_scrollTop, total, VISIBLE_ENTRIES);
        int end = std::min(total, start + VISIBLE_ENTRIES);

        ftxui::Elements elems;

        if (start > 0) {
            elems.push_back(
                ftxui::text("\u2191 " + std::to_string(start) + " more") |
                ftxui::dim | ftxui::bold
            );
        }

        for (int i = start; i < end; ++i) {
            elems.push_back(xRenderEntry(m_impl->entries[i]));
        }

        if (end < total) {
            elems.push_back(
                ftxui::text("\u2193 " + std::to_string(total - end) + " more") |
                ftxui::dim | ftxui::bold
            );
        }

        return ftxui::vbox(std::move(elems)) | ftxui::yframe | ftxui::vscroll_indicator;
    });

    auto eventHandler = ftxui::CatchEvent(m_impl->renderer, [this](ftxui::Event event) -> bool {
        if (event == ftxui::Event::PageUp) {
            scrollUp(VISIBLE_ENTRIES);
            return true;
        }
        if (event == ftxui::Event::PageDown) {
            scrollDown(VISIBLE_ENTRIES);
            return true;
        }
        if (event == ftxui::Event::Home) {
            scrollToTop();
            return true;
        }
        if (event == ftxui::Event::End) {
            scrollToBottom();
            return true;
        }
        return false;
    });

    ftxui::Components children;
    children.push_back(eventHandler);
    m_impl->scrollContainer = ftxui::Container::Vertical(std::move(children));
}

MessagePanel::~MessagePanel() = default;

ftxui::Component MessagePanel::component() const {
    return m_impl->scrollContainer;
}

int MessagePanel::append(const MessageEntry& entry) {
    m_impl->entries.push_back(entry);
    int idx = static_cast<int>(m_impl->entries.size()) - 1;
    m_impl->ensureScroll();
    return idx;
}

int MessagePanel::beginStreaming(MessageRole role) {
    MessageEntry entry;
    entry.role = role;
    entry.streaming = true;
    entry.timestamp = 0;
    m_impl->entries.push_back(entry);
    int idx = static_cast<int>(m_impl->entries.size()) - 1;
    m_impl->ensureScroll();
    return idx;
}

int MessagePanel::streamUpdate(int index, const std::string& text) {
    if (index < 0 || index >= static_cast<int>(m_impl->entries.size())) return -1;
    m_impl->entries[index].content = text;
    m_impl->ensureScroll();
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
    int idx = static_cast<int>(m_impl->entries.size()) - 1;
    m_impl->ensureScroll();
    return idx;
}

int MessagePanel::updateToolCall(int index, ToolState state, const std::string& output) {
    if (index < 0 || index >= static_cast<int>(m_impl->entries.size())) return -1;
    m_impl->entries[index].toolState = state;
    if (!output.empty()) {
        m_impl->entries[index].toolOutput = output;
    }
    return 0;
}

void MessagePanel::clear() {
    m_impl->entries.clear();
    m_impl->m_scrollTop = 0;
    m_impl->autoScroll = true;
}

void MessagePanel::scrollToBottom() {
    m_impl->autoScroll = true;
    m_impl->ensureScroll();
}

void MessagePanel::scrollUp(int n) {
    m_impl->autoScroll = false;
    m_impl->m_scrollTop = std::max(0, m_impl->m_scrollTop - n);
}

void MessagePanel::scrollDown(int n) {
    int total = static_cast<int>(m_impl->entries.size());
    int maxStart = std::max(0, total - VISIBLE_ENTRIES);
    m_impl->m_scrollTop = std::min(maxStart, m_impl->m_scrollTop + n);
    if (m_impl->m_scrollTop >= maxStart) {
        m_impl->autoScroll = true;
    }
}

void MessagePanel::scrollToTop() {
    m_impl->autoScroll = false;
    m_impl->m_scrollTop = 0;
}

int MessagePanel::scrollTop() const {
    return m_impl->m_scrollTop;
}

bool MessagePanel::isAtBottom() const {
    int total = static_cast<int>(m_impl->entries.size());
    int maxStart = std::max(0, total - VISIBLE_ENTRIES);
    return m_impl->autoScroll || total == 0 || m_impl->m_scrollTop >= maxStart;
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
        content = ftxui::text(entry.content);
    }

    auto label = roleLabel(role, entry.toolName);
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
        body.push_back(ftxui::text(entry.toolOutput) | ftxui::color(ftxui::Color::GrayDark));
    }

    return ftxui::vbox(std::move(body)) | ftxui::color(ftxui::Color::BlueLight);
}

ftxui::Element MessagePanel::xRenderStreamingPlaceholder(const MessageEntry& entry) const {
    auto cursor = ftxui::text("\u258C") | ftxui::blink;
    ftxui::Elements elems;
    elems.push_back(ftxui::text(entry.content));
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
