#include "input_panel.h"
#include "ftxui/component/component.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/component/component_options.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"

namespace a0::tui {

class InputPanel::Impl {
public:
    ftxui::Component realInput;     // the live input (rawInput | CatchEvent)
    ftxui::Component disabledInput; // Renderer placeholder when disabled
    ftxui::Component current;       // points to realInput or disabledInput

    std::function<void(const std::string&)> onSubmit;
    std::function<void()> onInterrupt;
    std::function<void(const std::string&)> onChange;
    std::vector<std::string> history;
    size_t historyPos = 0;
    std::string placeholder;
    std::string contentBuf;
};

InputPanel::InputPanel()
    : m_impl(std::make_unique<Impl>()) {
    m_impl->contentBuf.clear();
    m_enabled = true;

    // Enable multiline so pasted text with newlines renders correctly.
    // Only bare Enter (Event::Return) submits — \r/\n character events
    // from paste are blocked by the outer CatchEvent and never reach here.
    ftxui::InputOption option;
    option.multiline = true;
    option.on_change = [this] {
        if (m_impl->onChange) {
            m_impl->onChange(m_impl->contentBuf);
        }
    };

    auto rawInput = ftxui::Input(&m_impl->contentBuf, "", option);

    // CatchEvent to handle Enter submit after Input processes the key.
    auto eventCb = [this](ftxui::Event event) -> bool {
        if (event == ftxui::Event::Return) {
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

    m_impl->realInput = rawInput | ftxui::CatchEvent(eventCb);

    // Disabled renderer — shows "Waiting..." placeholder, ignores all input.
    m_impl->disabledInput = ftxui::Renderer([] {
        return ftxui::text(" Waiting for response...") | ftxui::dim;
    });

    m_impl->current = m_impl->realInput;
}

InputPanel::~InputPanel() = default;

ftxui::Component InputPanel::component() const {
    return m_impl->current;
}

void InputPanel::setOnSubmit(std::function<void(const std::string&)> cb) {
    m_impl->onSubmit = std::move(cb);
}

void InputPanel::setOnInterrupt(std::function<void()> cb) {
    m_impl->onInterrupt = std::move(cb);
}

void InputPanel::setOnChange(std::function<void(const std::string&)> cb) {
    m_impl->onChange = std::move(cb);
}

void InputPanel::insertText(const std::string& text) {
    m_impl->contentBuf += text;
    // Move cursor to end of content so further typing appears after
    // the inserted text, not before it or in the middle.
    if (m_impl->realInput) {
        m_impl->realInput->OnEvent(ftxui::Event::End);
    }
    if (m_impl->onChange) {
        m_impl->onChange(m_impl->contentBuf);
    }
}

void InputPanel::setEnabled(bool enabled) {
    m_enabled = enabled;
    if (enabled) {
        m_impl->current = m_impl->realInput;
    } else {
        m_impl->current = m_impl->disabledInput;
    }
}

void InputPanel::setPlaceholder(const std::string& text) {
    m_impl->placeholder = text;
}

void InputPanel::clear() {
    m_impl->contentBuf.clear();
}

void InputPanel::focus() {
    if (m_enabled && m_impl->realInput) {
        m_impl->realInput->TakeFocus();
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
