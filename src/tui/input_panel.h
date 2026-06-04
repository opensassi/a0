#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include "ftxui/component/component.hpp"

namespace a0::tui {

class InputPanel {
public:
    InputPanel();
    virtual ~InputPanel();

    ftxui::Component component() const;

    void setOnSubmit(std::function<void(const std::string&)> cb);
    void setOnInterrupt(std::function<void()> cb);
    void setEnabled(bool enabled);
    void setPlaceholder(const std::string& text);
    void clear();
    void focus();

    int addHistory(const std::string& prompt);
    int loadHistory(const std::string& path);
    int saveHistory(const std::string& path);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;

    static constexpr size_t MAX_HISTORY = 50;
};

} // namespace a0::tui
