#include "tui/styles.h"
#include <gtest/gtest.h>
#include "ftxui/dom/elements.hpp"

using namespace a0::tui;

TEST(TuiStylesTest, RoleDecoratorUser) {
    auto dec = roleDecorator(MessageRole::User);
    EXPECT_NE(&dec, nullptr);
}

TEST(TuiStylesTest, RoleDecoratorAssistant) {
    auto dec = roleDecorator(MessageRole::Assistant);
    EXPECT_NE(&dec, nullptr);
}

TEST(TuiStylesTest, RoleDecoratorTool) {
    auto dec = roleDecorator(MessageRole::Tool);
    EXPECT_NE(&dec, nullptr);
}

TEST(TuiStylesTest, RoleDecoratorSystem) {
    auto dec = roleDecorator(MessageRole::System);
    EXPECT_NE(&dec, nullptr);
}

TEST(TuiStylesTest, RoleDecoratorError) {
    auto dec = roleDecorator(MessageRole::Error);
    EXPECT_NE(&dec, nullptr);
}

TEST(TuiStylesTest, RoleLabelUser) {
    auto label = roleLabel(MessageRole::User);
    EXPECT_EQ(label, "\u250C\u2500 You");
}

TEST(TuiStylesTest, RoleLabelAssistant) {
    auto label = roleLabel(MessageRole::Assistant);
    EXPECT_EQ(label, "\u250C\u2500 Assistant");
}

TEST(TuiStylesTest, RoleLabelTool) {
    auto label = roleLabel(MessageRole::Tool, "glob");
    EXPECT_TRUE(label.find("glob") != std::string::npos);
}

TEST(TuiStylesTest, StyleDecoratorsNonNull) {
    EXPECT_NE(&style::codeBlock, nullptr);
    EXPECT_NE(&style::inlineCode, nullptr);
    EXPECT_NE(&style::heading1, nullptr);
    EXPECT_NE(&style::heading2, nullptr);
    EXPECT_NE(&style::dimmed, nullptr);
    EXPECT_NE(&style::link, nullptr);
    EXPECT_NE(&style::toolOutput, nullptr);
    EXPECT_NE(&style::toolOutputStderr, nullptr);
    EXPECT_NE(&style::statusGood, nullptr);
    EXPECT_NE(&style::statusBad, nullptr);
}

TEST(TuiStylesTest, ConstantsDefined) {
    EXPECT_EQ(INPUT_PANEL_MIN_HEIGHT, 3);
    EXPECT_EQ(STATUS_BAR_HEIGHT, 1);
    EXPECT_EQ(SIDEBAR_WIDTH, 42);
    EXPECT_EQ(DIALOG_MIN_WIDTH, 60);
    EXPECT_EQ(DIALOG_MAX_WIDTH, 116);
}
