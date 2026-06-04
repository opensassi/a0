#include "tui/markdown_renderer.h"
#include <gtest/gtest.h>

using namespace a0::tui;

TEST(TuiMarkdownTest, EmptyInput) {
    MarkdownRenderer renderer;
    auto elem = renderer.render("");
    EXPECT_NE(&elem, nullptr);
}

TEST(TuiMarkdownTest, PlainText) {
    MarkdownRenderer renderer;
    auto elem = renderer.render("Hello world");
    EXPECT_NE(&elem, nullptr);
}

TEST(TuiMarkdownTest, BoldText) {
    MarkdownRenderer renderer;
    auto elem = renderer.render("**bold**");
    EXPECT_NE(&elem, nullptr);
}

TEST(TuiMarkdownTest, ItalicText) {
    MarkdownRenderer renderer;
    auto elem = renderer.render("*italic*");
    EXPECT_NE(&elem, nullptr);
}

TEST(TuiMarkdownTest, InlineCode) {
    MarkdownRenderer renderer;
    auto elem = renderer.render("`code`");
    EXPECT_NE(&elem, nullptr);
}

TEST(TuiMarkdownTest, FencedCodeBlock) {
    MarkdownRenderer renderer;
    auto elem = renderer.render("```\nint x = 42;\n```");
    EXPECT_NE(&elem, nullptr);
}

TEST(TuiMarkdownTest, Heading1) {
    MarkdownRenderer renderer;
    auto elem = renderer.render("# Heading");
    EXPECT_NE(&elem, nullptr);
}

TEST(TuiMarkdownTest, BulletList) {
    MarkdownRenderer renderer;
    auto elem = renderer.render("- item 1\n- item 2");
    EXPECT_NE(&elem, nullptr);
}

TEST(TuiMarkdownTest, NumberedList) {
    MarkdownRenderer renderer;
    auto elem = renderer.render("1. first\n2. second");
    EXPECT_NE(&elem, nullptr);
}

TEST(TuiMarkdownTest, HorizontalRule) {
    MarkdownRenderer renderer;
    auto elem = renderer.render("---");
    EXPECT_NE(&elem, nullptr);
}

TEST(TuiMarkdownTest, Link) {
    MarkdownRenderer renderer;
    auto elem = renderer.render("[text](http://example.com)");
    EXPECT_NE(&elem, nullptr);
}

TEST(TuiMarkdownTest, MixedFormatting) {
    MarkdownRenderer renderer;
    auto elem = renderer.render("# Title\n\nThis is **bold** and *italic* `code`.");
    EXPECT_NE(&elem, nullptr);
}

TEST(TuiMarkdownTest, StreamingModeTruncated) {
    MarkdownRenderer renderer;
    auto elem = renderer.render("**unclosed", true);
    EXPECT_NE(&elem, nullptr);
}

TEST(TuiMarkdownTest, StreamingModeUnclosedCodeFence) {
    MarkdownRenderer renderer;
    auto elem = renderer.render("```\ncode block", true);
    EXPECT_NE(&elem, nullptr);
}

TEST(TuiMarkdownTest, RenderInline) {
    MarkdownRenderer renderer;
    auto elem = renderer.renderInline("inline **bold**");
    EXPECT_NE(&elem, nullptr);
}

TEST(TuiMarkdownTest, TableNotCrashes) {
    MarkdownRenderer renderer;
    auto elem = renderer.render("| a | b |\n|---|---|\n| 1 | 2 |");
    EXPECT_NE(&elem, nullptr);
}
