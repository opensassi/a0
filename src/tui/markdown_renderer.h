#pragma once

#include <string>
#include <memory>
#include "ftxui/dom/elements.hpp"

namespace a0::tui {

class MarkdownRenderer {
public:
    MarkdownRenderer();
    virtual ~MarkdownRenderer();

    ftxui::Element render(const std::string& md, bool streaming = false);
    ftxui::Element renderInline(const std::string& md);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace a0::tui
