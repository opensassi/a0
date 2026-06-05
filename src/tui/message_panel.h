#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "styles.h"
#include "../mpsc.h"

namespace a0::tui {

struct MessageEntry {
    MessageRole role;
    std::string content;
    std::string toolName;
    ToolState toolState = ToolState::Completed;
    std::string toolOutput;
    int64_t timestamp = 0;
    bool collapsed = false;
    bool streaming = false;
    int64_t sessionId = 0;
};

class MessagePanel {
public:
    MessagePanel();
    virtual ~MessagePanel();

    ftxui::Component component() const;

    int append(const MessageEntry& entry);
    int beginStreaming(MessageRole role);
    int streamUpdate(int index, const std::string& text);
    int endStream(int index);

    int appendToolCall(const std::string& name,
                       ToolState state,
                       const std::string& output = "");

    int updateToolCall(int index, ToolState state, const std::string& output);

    void clear();
    void scrollToBottom();

    int loadHistory(const std::vector<::a0::mpsc::SessionMessage>& messages);

    size_t count() const;
    void scrollUp(int n = 1);
    void scrollDown(int n = 1);
    void scrollToTop();
    int scrollTop() const;
    bool isAtBottom() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;

    ftxui::Element xRenderEntry(const MessageEntry& entry) const;
    ftxui::Element xRenderToolBlock(const MessageEntry& entry) const;
    ftxui::Element xRenderStreamingPlaceholder(const MessageEntry& entry) const;
    ftxui::Element xRenderCollapsedToggle(const MessageEntry& entry) const;

    static constexpr int VISIBLE_ENTRIES = 8;
};

} // namespace a0::tui
