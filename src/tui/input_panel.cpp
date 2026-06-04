#include "input_panel.h"
#include "ftxui/component/component.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/component/component_options.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"

namespace a0::tui {

class InputPanel::Impl {
public:
    ftxui::Component input;
    std::function<void(const std::string&)> onSubmit;
    std::function<void()> onInterrupt;
    std::vector<std::string> history;
    size_t historyPos = 0;
    bool enabled = true;
    std::string placeholder;
    std::string contentBuf;
};

InputPanel::InputPanel()
    : m_impl(std::make_unique<Impl>()) {
    m_impl->contentBuf.clear();

    auto rawInput = ftxui::Input(&m_impl->contentBuf, "");

    // CatchEvent to handle Enter submit after Input processes the key
    auto eventCb = [this](ftxui::Event event) -> bool {
        // Terminals send \r (CR) on Enter; Event::Return is \n (LF).
        if (event == ftxui::Event::Return
            || (event.is_character() && !event.character().empty()
                && (event.character()[0] == '\r' || event.character()[0] == '\n'))) {
            auto text = m_impl->contentBuf;
            if (!text.empty()) {
                addHistory(text);
                if (m_impl->onSubmit) {
                    m_impl->onSubmit(text);
                    m_impl->contentBuf.clear();
                }
            }
            return true;
        }
        if (event.is_character() && !event.character().empty()
            && event.character()[0] == 3) {
            if (m_impl->onInterrupt)
                m_impl->onInterrupt();
            return true;
        }
        return false;
    };

    // Try with an explicit function pointer approach
    // Use the ComponentDecorator overload: CatchEvent(lambda) returns a ComponentDecorator
    // Then apply it: rawInput | CatchEvent(lambda)
    m_impl->input = rawInput | ftxui::CatchEvent(eventCb);
}

InputPanel::~InputPanel() = default;

ftxui::Component InputPanel::component() const {
    return m_impl->input;
}

void InputPanel::setOnSubmit(std::function<void(const std::string&)> cb) {
    m_impl->onSubmit = std::move(cb);
}

void InputPanel::setOnInterrupt(std::function<void()> cb) {
    m_impl->onInterrupt = std::move(cb);
}

void InputPanel::setEnabled(bool enabled) {
    m_impl->enabled = enabled;
}

void InputPanel::setPlaceholder(const std::string& text) {
    m_impl->placeholder = text;
}

void InputPanel::clear() {
    m_impl->contentBuf.clear();
}

void InputPanel::focus() {
    if (m_impl->input) {
        m_impl->input->TakeFocus();
    }
}

int InputPanel::addHistory(const std::string& prompt) {
    m_impl->history.push_back(prompt);
    if (m_impl->history.size() > MAX_HISTORY) {
        m_impl->history.erase(m_impl->history.begin());
    }
    m_impl->historyPos = m_impl->history.size();
    return static_cast<int>(m_impl->history.size() - 1);
}

int InputPanel::loadHistory(const std::string& path) {
    (void)path;
    return -1;
}

int InputPanel::saveHistory(const std::string& path) {
    (void)path;
    return -1;
}

} // namespace a0::tui
