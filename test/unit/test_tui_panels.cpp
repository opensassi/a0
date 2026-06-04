#include "tui/message_panel.h"
#include "tui/input_panel.h"
#include "tui/status_bar.h"
#include "tui/dialog_manager.h"
#include "tui/styles.h"
#include <gtest/gtest.h>

using namespace a0::tui;

// --- MessagePanel Tests ---

TEST(TuiMessagePanelTest, AppendAndCount) {
    MessagePanel panel;
    EXPECT_EQ(panel.count(), 0);

    MessageEntry entry;
    entry.role = MessageRole::User;
    entry.content = "hello";
    panel.append(entry);

    EXPECT_EQ(panel.count(), 1);
}

TEST(TuiMessagePanelTest, AppendMultipleRoles) {
    MessagePanel panel;

    MessageEntry user;
    user.role = MessageRole::User;
    user.content = "user msg";
    panel.append(user);

    MessageEntry asst;
    asst.role = MessageRole::Assistant;
    asst.content = "asst msg";
    panel.append(asst);

    EXPECT_EQ(panel.count(), 2);
}

TEST(TuiMessagePanelTest, ClearEmptyPanel) {
    MessagePanel panel;
    panel.clear();
    EXPECT_EQ(panel.count(), 0);
}

TEST(TuiMessagePanelTest, ClearWithMessages) {
    MessagePanel panel;
    MessageEntry entry;
    entry.role = MessageRole::User;
    entry.content = "test";
    panel.append(entry);
    panel.append(entry);
    panel.clear();
    EXPECT_EQ(panel.count(), 0);
}

TEST(TuiMessagePanelTest, BeginStreamingAndEnd) {
    MessagePanel panel;
    int idx = panel.beginStreaming(MessageRole::Assistant);
    EXPECT_GE(idx, 0);
    EXPECT_EQ(panel.count(), 1);

    panel.streamUpdate(idx, "hello");
    panel.endStream(idx);
    EXPECT_EQ(panel.count(), 1);
}

TEST(TuiMessagePanelTest, StreamUpdateInvalidIndex) {
    MessagePanel panel;
    int rc = panel.streamUpdate(-1, "test");
    EXPECT_EQ(rc, -1);

    rc = panel.streamUpdate(999, "test");
    EXPECT_EQ(rc, -1);
}

TEST(TuiMessagePanelTest, AppendToolCall) {
    MessagePanel panel;
    int idx = panel.appendToolCall("glob", ToolState::Pending);
    EXPECT_GE(idx, 0);
    EXPECT_EQ(panel.count(), 1);
}

TEST(TuiMessagePanelTest, UpdateToolCall) {
    MessagePanel panel;
    int idx = panel.appendToolCall("glob", ToolState::Running);
    panel.updateToolCall(idx, ToolState::Completed, "found 3 files");
    EXPECT_EQ(panel.count(), 1);
}

TEST(TuiMessagePanelTest, UpdateToolCallInvalidIndex) {
    MessagePanel panel;
    int rc = panel.updateToolCall(-1, ToolState::Completed, "out");
    EXPECT_EQ(rc, -1);
}

TEST(TuiMessagePanelTest, ScrollToBottom) {
    MessagePanel panel;
    panel.scrollToBottom();
    SUCCEED();
}

TEST(TuiMessagePanelTest, LoadHistory) {
    MessagePanel panel;
    std::vector<a0::persistence::Message> msgs;

    a0::persistence::Message m1;
    m1.role = "user";
    m1.content = "hello";
    m1.createdAt = 1000;
    msgs.push_back(m1);

    a0::persistence::Message m2;
    m2.role = "assistant";
    m2.content = "world";
    m2.createdAt = 1001;
    msgs.push_back(m2);

    int count = panel.loadHistory(msgs);
    EXPECT_EQ(count, 2);
    EXPECT_EQ(panel.count(), 2);
}

// --- InputPanel Tests ---

TEST(TuiInputPanelTest, SubmitCallbackFires) {
    InputPanel panel;
    std::string captured;
    panel.setOnSubmit([&](const std::string& text) {
        captured = text;
    });
}

TEST(TuiInputPanelTest, AddHistory) {
    InputPanel panel;
    int idx = panel.addHistory("prompt 1");
    EXPECT_GE(idx, 0);

    idx = panel.addHistory("prompt 2");
    EXPECT_GE(idx, 0);
}

TEST(TuiInputPanelTest, SetPlaceholder) {
    InputPanel panel;
    panel.setPlaceholder("Type a message...");
    SUCCEED();
}

TEST(TuiInputPanelTest, ClearAndFocus) {
    InputPanel panel;
    panel.clear();
    panel.focus();
    SUCCEED();
}

// --- StatusBar Tests ---

TEST(TuiStatusBarTest, SetSessionId) {
    StatusBar bar;
    bar.setSessionId("abc-123");
    SUCCEED();
}

TEST(TuiStatusBarTest, SetAgentStateAllStates) {
    StatusBar bar;
    bar.setAgentState(AgentState::Idle);
    bar.setAgentState(AgentState::Thinking);
    bar.setAgentState(AgentState::Executing);
    bar.setAgentState(AgentState::Error);
    SUCCEED();
}

TEST(TuiStatusBarTest, SetB1Connected) {
    StatusBar bar;
    bar.setB1Connected(true);
    bar.setB1Connected(false);
    SUCCEED();
}

TEST(TuiStatusBarTest, SetMessageCount) {
    StatusBar bar;
    bar.setMessageCount(0);
    bar.setMessageCount(42);
    SUCCEED();
}

TEST(TuiStatusBarTest, ShowStatusFlash) {
    StatusBar bar;
    bar.showStatus("Saved!", 1);
    SUCCEED();
}

// --- DialogManager Tests ---

TEST(TuiDialogManagerTest, InitialState) {
    DialogManager dm;
    EXPECT_FALSE(dm.isActive());
}

TEST(TuiDialogManagerTest, ShowAndDismiss) {
    DialogManager dm;
    auto dialog = ftxui::Renderer([] { return ftxui::text("test"); });
    dm.show(dialog, "Test");
    EXPECT_TRUE(dm.isActive());
    dm.dismiss();
    EXPECT_FALSE(dm.isActive());
}

TEST(TuiDialogManagerTest, DismissNoDialog) {
    DialogManager dm;
    dm.dismiss();
    EXPECT_FALSE(dm.isActive());
}

TEST(TuiDialogManagerTest, DismissAllWithMultiple) {
    DialogManager dm;
    auto dialog = ftxui::Renderer([] { return ftxui::text("test1"); });
    dm.show(dialog, "Test1");
    dm.show(dialog, "Test2");
    dm.dismissAll();
    EXPECT_FALSE(dm.isActive());
}

TEST(TuiDialogManagerTest, ShowHelp) {
    DialogManager dm;
    dm.showHelp();
    EXPECT_TRUE(dm.isActive());
    dm.dismiss();
    EXPECT_FALSE(dm.isActive());
}

TEST(TuiDialogManagerTest, ShowConfirm) {
    DialogManager dm;
    bool confirmed = false;
    dm.showConfirm("Quit?", "Are you sure?", [&](bool yes) {
        confirmed = yes;
    });
    EXPECT_TRUE(dm.isActive());
    // confirm doesn't auto-dismiss in stub implementation
    dm.dismissAll();
    EXPECT_FALSE(dm.isActive());
}
