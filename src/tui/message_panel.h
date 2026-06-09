#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "styles.h"
#include "shared/mpsc.h"

namespace a0::tui {

struct MessageEntry {
    MessageRole role;
    std::string content;
    std::string toolName;
    std::string toolArgs;
    ToolState toolState = ToolState::Completed;
    std::string toolOutput;
    int64_t timestamp = 0;
    bool collapsed = true;
    bool streaming = false;
    int64_t sessionId = 0;
    std::vector<MessageEntry> children;
};

class MessagePanel {
public:
    MessagePanel();
    virtual ~MessagePanel();

    ftxui::Component component() const;

    int append(const MessageEntry& entry);
    void clear();

    int beginAssistant();
    int appendOrUpdateAssistantText(int asstIdx, const std::string& text);
    int endCurrentAssistantText(int asstIdx);
    int appendAssistantTool(int asstIdx, const std::string& name,
                            ToolState state, const std::string& args = "");
    int updateLastAssistantTool(int asstIdx, ToolState state,
                                const std::string& output);
    int finalizeAssistant(int asstIdx);

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

    ftxui::Element xRenderEntry(int entryIdx);
    ftxui::Element xRenderAssistant(int entryIdx);
    ftxui::Element xRenderToolBlock(const MessageEntry& entry) const;
    ftxui::Element xRenderStreamingPlaceholder(const MessageEntry& entry) const;
};

} // namespace a0::tui
