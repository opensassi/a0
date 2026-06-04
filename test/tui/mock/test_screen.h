#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/component/loop.hpp"

namespace a0::tui::test {

class TestScreen {
public:
    TestScreen(int width = 80, int height = 24);
    ~TestScreen();

    void start(ftxui::Component component);
    void stop();

    void sendKey(const std::string& key);
    void sendChar(char c);
    void sendText(const std::string& text);

    std::vector<std::string> captureScreen();
    std::string captureText();

    bool waitFor(std::function<bool(const std::string&)> predicate,
                 int timeoutMs = 3000);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace a0::tui::test
