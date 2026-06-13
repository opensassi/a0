#include "message_panel.h"
#include "ftxui/component/component.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include <algorithm>
#include <sstream>

namespace a0::tui {

struct ToolHit {
    int entryIdx;
    int childIdx;
    ftxui::Box box;
};

class MessagePanel::Impl {
public:
    std::vector<MessageEntry> entries;
    ftxui::Component renderer;
    int focusIndex = 0;
    bool autoScroll = true;
    std::vector<ftxui::Box> entryBoxes;
    std::vector<ToolHit> toolHits;
};

MessagePanel::MessagePanel()
    : m_impl(std::make_unique<Impl>()) {
    m_impl->renderer = ftxui::CatchEvent(
        ftxui::Renderer([this] {
            int total = static_cast<int>(m_impl->entries.size());

            if (total == 0) {
                return ftxui::vbox({
                    ftxui::text("") | ftxui::size(ftxui::HEIGHT, ftxui::GREATER_THAN, 1)
                });
            }

            int focusIdx;
            if (m_impl->autoScroll) {
                focusIdx = total - 1;
            } else {
                focusIdx = std::min(m_impl->focusIndex, total - 1);
            }

            m_impl->entryBoxes.resize(total);

            m_impl->toolHits.clear();
            ftxui::Elements elems;
            for (int i = 0; i < total; ++i) {
                auto entry = xRenderEntry(i);
                if (i == focusIdx) {
                    entry = entry | ftxui::focus;
                }
                entry = entry | ftxui::reflect(m_impl->entryBoxes[i]);
                elems.push_back(std::move(entry));
            }

            return ftxui::vbox(std::move(elems)) | ftxui::yframe | ftxui::vscroll_indicator;
        }),
        [this](ftxui::Event event) -> bool {
            if (!event.is_mouse()) return false;
            auto& mouse = event.mouse();
            if (mouse.button == ftxui::Mouse::Left &&
                mouse.motion == ftxui::Mouse::Released) {
                for (int i = 0; i < static_cast<int>(m_impl->entryBoxes.size()); ++i) {
                    auto& b = m_impl->entryBoxes[i];
                    if (mouse.x >= b.x_min && mouse.x <= b.x_max &&
                        mouse.y >= b.y_min && mouse.y <= b.y_max) {
                        if (m_impl->entries[i].role == MessageRole::Tool &&
                            !m_impl->entries[i].toolName.empty()) {
                            m_impl->entries[i].collapsed = !m_impl->entries[i].collapsed;
                        }
                    }
                }
                // Check tool child hits inside assistant entries
                if (!m_impl->toolHits.empty()) {
                    for (auto& h : m_impl->toolHits) {
                        if (mouse.x >= h.box.x_min && mouse.x <= h.box.x_max &&
                            mouse.y >= h.box.y_min && mouse.y <= h.box.y_max &&
                            h.entryIdx >= 0 &&
                            h.childIdx < static_cast<int>(m_impl->entries[h.entryIdx].children.size())) {
                            m_impl->entries[h.entryIdx].children[h.childIdx].collapsed ^= true;
                            break;
                        }
                    }
                }
            }
            return false;
        }
    );
}

MessagePanel::~MessagePanel() = default;

ftxui::Component MessagePanel::component() const {
    return m_impl->renderer;
}

int MessagePanel::append(const MessageEntry& entry) {
    m_impl->entries.push_back(entry);
    return static_cast<int>(m_impl->entries.size()) - 1;
}

void MessagePanel::clear() {
    m_impl->entries.clear();
    m_impl->focusIndex = 0;
    m_impl->autoScroll = true;
    m_impl->entryBoxes.clear();
}

int MessagePanel::beginAssistant() {
    MessageEntry entry;
    entry.role = MessageRole::Assistant;
    entry.streaming = true;
    m_impl->entries.push_back(entry);
    return static_cast<int>(m_impl->entries.size()) - 1;
}

int MessagePanel::appendOrUpdateAssistantText(int asstIdx, const std::string& text) {
    if (asstIdx < 0 || asstIdx >= static_cast<int>(m_impl->entries.size())) return -1;
    auto& asst = m_impl->entries[asstIdx];
    if (asst.role != MessageRole::Assistant) return -1;

    // Look for an existing streaming assistant child (from current or prior round)
    for (int i = static_cast<int>(asst.children.size()) - 1; i >= 0; --i) {
        if (asst.children[i].role == MessageRole::Assistant) {
            asst.children[i].content = text;
            asst.children[i].streaming = true;
            return 0;
        }
    }
    // No existing assistant child — push as child only when tool children exist
    // so the text renders BELOW the tools (chronologically correct: tools execute
    // before the assistant speaks the result).
    if (asst.children.empty()) {
        // Round 1 text (before any tools) — use entry content
        asst.content = text;
    } else {
        // Round 2+ text (after tool execution) — push as child below tools
        MessageEntry child;
        child.role = MessageRole::Assistant;
        child.content = text;
        child.streaming = true;
        asst.children.push_back(std::move(child));
    }
    asst.streaming = true;
    return 0;
}

int MessagePanel::endCurrentAssistantText(int asstIdx) {
    if (asstIdx < 0 || asstIdx >= static_cast<int>(m_impl->entries.size())) return -1;
    auto& asst = m_impl->entries[asstIdx];
    if (asst.role != MessageRole::Assistant) return -1;
    for (int i = static_cast<int>(asst.children.size()) - 1; i >= 0; --i) {
        if (asst.children[i].role == MessageRole::Assistant && asst.children[i].streaming) {
            asst.children[i].streaming = false;
            break;
        }
    }
    return 0;
}

int MessagePanel::finalizeAssistant(int asstIdx) {
    if (asstIdx < 0 || asstIdx >= static_cast<int>(m_impl->entries.size())) return -1;
    auto& asst = m_impl->entries[asstIdx];
    if (asst.role != MessageRole::Assistant) return -1;
    for (int i = static_cast<int>(asst.children.size()) - 1; i >= 0; --i) {
        if (asst.children[i].role == MessageRole::Assistant && asst.children[i].streaming) {
            asst.children[i].streaming = false;
            break;
        }
    }
    asst.streaming = false;
    return 0;
}

int MessagePanel::appendAssistantTool(int asstIdx, const std::string& name,
                                       ToolState state, const std::string& args) {
    if (asstIdx < 0 || asstIdx >= static_cast<int>(m_impl->entries.size())) return -1;
    auto& asst = m_impl->entries[asstIdx];
    if (asst.role != MessageRole::Assistant) return -1;

    MessageEntry child;
    child.role = MessageRole::Tool;
    child.toolName = name;
    child.toolState = state;
    child.toolArgs = args;
    child.collapsed = true;
    asst.children.push_back(std::move(child));
    return static_cast<int>(asst.children.size()) - 1;
}

int MessagePanel::updateLastAssistantTool(int asstIdx, ToolState state,
                                           const std::string& output) {
    if (asstIdx < 0 || asstIdx >= static_cast<int>(m_impl->entries.size())) return -1;
    auto& asst = m_impl->entries[asstIdx];
    if (asst.role != MessageRole::Assistant) return -1;

    for (int i = static_cast<int>(asst.children.size()) - 1; i >= 0; --i) {
        auto& child = asst.children[i];
        if (child.role == MessageRole::Tool && child.toolState == ToolState::Running) {
            child.toolState = state;
            if (!output.empty()) child.toolOutput = output;
            return i;
        }
    }
    return -1;
}

int MessagePanel::updateLastAssistantToolOutput(int asstIdx, const std::string& text) {
    if (asstIdx < 0 || asstIdx >= static_cast<int>(m_impl->entries.size())) return -1;
    auto& asst = m_impl->entries[asstIdx];
    if (asst.role != MessageRole::Assistant) return -1;

    for (int i = static_cast<int>(asst.children.size()) - 1; i >= 0; --i) {
        auto& child = asst.children[i];
        if (child.role == MessageRole::Tool && child.toolState == ToolState::Running) {
            child.toolOutput += text;
            return i;
        }
    }
    return -1;
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
    clear();
    for (const auto& pm : messages) {
        MessageEntry entry;
        if (pm.role == "user") entry.role = MessageRole::User;
        else if (pm.role == "assistant") entry.role = MessageRole::Assistant;
        else if (pm.role == "tool") {
            entry.role = MessageRole::Tool;
            entry.toolName = pm.name;
            entry.toolOutput = pm.content;
            entry.collapsed = true;
            entry.toolState = ToolState::Completed;
        } else if (pm.role == "system") entry.role = MessageRole::System;
        else entry.role = MessageRole::Error;
        if (entry.role != MessageRole::Tool) {
            entry.content = pm.content;
        }
        entry.timestamp = pm.createdAt;
        m_impl->entries.push_back(entry);
    }
    return static_cast<int>(messages.size());
}

size_t MessagePanel::count() const {
    return m_impl->entries.size();
}

ftxui::Element MessagePanel::xRenderEntry(int entryIdx) {
    auto& entry = m_impl->entries[entryIdx];

    if (entry.role == MessageRole::Assistant) {
        return xRenderAssistant(entryIdx);
    }

    ftxui::Element content;
    if (entry.role == MessageRole::Tool && !entry.toolName.empty()) {
        content = xRenderToolBlock(entry);
    } else if (entry.streaming) {
        content = xRenderStreamingPlaceholder(entry);
    } else {
        content = ftxui::paragraph(entry.content);
    }

    if (entry.role == MessageRole::Tool && !entry.toolName.empty()) {
        ftxui::Elements elems;
        if (content) elems.push_back(content);
        return ftxui::vbox(std::move(elems));
    }

    std::string label = roleLabel(entry.role, entry.toolName);
    ftxui::Elements elems;
    elems.push_back(ftxui::text(label) | roleDecorator(entry.role));
    if (content) elems.push_back(content);
    elems.push_back(ftxui::text(""));
    return ftxui::vbox(std::move(elems));
}

ftxui::Element MessagePanel::xRenderAssistant(int entryIdx) {
    auto& entry = m_impl->entries[entryIdx];

    ftxui::Elements elems;
    elems.push_back(ftxui::text(roleLabel(MessageRole::Assistant)) | roleDecorator(MessageRole::Assistant));

    if (!entry.children.empty()) {
        // Render entry-level content first (e.g. initial thinking text)
        if (!entry.content.empty()) {
            elems.push_back(ftxui::paragraph(entry.content));
        } else if (entry.streaming) {
            elems.push_back(xRenderStreamingPlaceholder(entry));
        }
        // Then render children (tool calls + additional assistant text)
        // Pre-allocate toolHits capacity so push_back doesn't reallocate the
        // vector midway through the loop — ftxui::reflect stores a reference
        // to the Box inside toolHits, and reallocation would dangle it.
        {
            size_t toolChildCount = 0;
            for (const auto& c : entry.children)
                if (c.role == MessageRole::Tool) toolChildCount++;
            m_impl->toolHits.reserve(m_impl->toolHits.size() + toolChildCount);
        }
        for (int ci = 0; ci < static_cast<int>(entry.children.size()); ++ci) {
            auto& child = entry.children[ci];
            if (child.role == MessageRole::Assistant && child.streaming) {
                elems.push_back(xRenderStreamingPlaceholder(child));
            } else if (child.role == MessageRole::Assistant) {
                elems.push_back(ftxui::paragraph(child.content));
            } else if (child.role == MessageRole::Tool) {
                m_impl->toolHits.push_back({entryIdx, ci, ftxui::Box{}});
                elems.push_back(xRenderToolBlock(child) | ftxui::reflect(m_impl->toolHits.back().box));
            }
        }
    } else if (entry.streaming) {
        elems.push_back(xRenderStreamingPlaceholder(entry));
    } else if (!entry.content.empty()) {
        elems.push_back(ftxui::paragraph(entry.content));
    }

    elems.push_back(ftxui::text(""));
    return ftxui::vbox(std::move(elems));
}

ftxui::Element MessagePanel::xRenderStreamingPlaceholder(const MessageEntry& entry) const {
    if (entry.content.empty()) {
        return ftxui::text("\u23F3 thinking") | ftxui::dim;
    }
    return ftxui::paragraph(entry.content);
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

    std::string headerText = "\U0001F527 " + entry.toolName;
    if (!entry.toolArgs.empty()) {
        headerText += " " + entry.toolArgs;
    }
    headerText += "  " + stateStr;
    auto header = ftxui::paragraph(headerText) | ftxui::bold | ftxui::color(ftxui::Color::BlueLight);

    if (entry.collapsed || entry.toolOutput.empty()) {
        return ftxui::vbox({
            header,
        });
    }

    ftxui::Elements body;
    body.push_back(header);
    body.push_back(ftxui::paragraph(entry.toolOutput) | ftxui::color(ftxui::Color::GrayDark));
    body.push_back(ftxui::text(""));
    return ftxui::vbox(std::move(body)) | ftxui::color(ftxui::Color::BlueLight);
}

} // namespace a0::tui
