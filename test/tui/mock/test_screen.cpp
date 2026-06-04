#include "test_screen.h"
#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/component/loop.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "ftxui/screen/terminal.hpp"
#include <thread>
#include <chrono>
#include <atomic>

namespace a0::tui::test {

class TestScreen::Impl {
public:
    int width;
    int height;
    ftxui::ScreenInteractive screen;
    ftxui::Component component;
    ftxui::Loop* loop = nullptr;
    std::thread loopThread;
    std::atomic<bool> running{false};

    Impl(int w, int h)
        : width(w), height(h)
        , screen(ftxui::ScreenInteractive::FixedSize(w, h)) {}

    void runLoop() {
        if (loop) {
            loop->Run();
        }
    }

    ftxui::Event keyToEvent(const std::string& key) {
        if (key == "Enter")    return ftxui::Event::Return;
        if (key == "Up")       return ftxui::Event::ArrowUp;
        if (key == "Down")     return ftxui::Event::ArrowDown;
        if (key == "Left")     return ftxui::Event::ArrowLeft;
        if (key == "Right")    return ftxui::Event::ArrowRight;
        if (key == "Escape")   return ftxui::Event::Escape;
        if (key == "Backspace") return ftxui::Event::Backspace;
        if (key == "Tab")      return ftxui::Event::Tab;
        if (key == "Ctrl+c" || key == "Ctrl+C") return ftxui::Event::CtrlC;
        if (key == "Ctrl+q" || key == "Ctrl+Q") return ftxui::Event::CtrlQ;
        if (key.size() == 1) {
            return ftxui::Event::Character(key[0]);
        }
        return ftxui::Event::Character(key);
    }
};

TestScreen::TestScreen(int width, int height)
    : m_impl(std::make_unique<Impl>(width, height)) {}

TestScreen::~TestScreen() {
    stop();
}

void TestScreen::start(ftxui::Component component) {
    if (m_impl->running) return;

    m_impl->component = component;
    m_impl->loop = new ftxui::Loop(&m_impl->screen, component);
    m_impl->running = true;

    m_impl->loopThread = std::thread([this] {
        m_impl->runLoop();
        m_impl->running = false;
    });
}

void TestScreen::stop() {
    if (!m_impl->running) return;
    m_impl->screen.ExitLoopClosure()();
    if (m_impl->loopThread.joinable()) {
        m_impl->loopThread.join();
    }
    delete m_impl->loop;
    m_impl->loop = nullptr;
    m_impl->running = false;
}

void TestScreen::sendKey(const std::string& key) {
    if (!m_impl->running) return;
    auto event = m_impl->keyToEvent(key);
    m_impl->screen.PostEvent(event);
}

void TestScreen::sendChar(char c) {
    sendKey(std::string(1, c));
}

void TestScreen::sendText(const std::string& text) {
    for (char c : text) {
        sendChar(c);
    }
}

std::vector<std::string> TestScreen::captureScreen() {
    if (!m_impl->component) return {};
    auto doc = m_impl->component->Render();
    ftxui::Screen renderScreen = ftxui::Screen::Create(
        ftxui::Dimension::Fixed(m_impl->width),
        ftxui::Dimension::Fixed(m_impl->height));
    ftxui::Render(renderScreen, doc);

    std::vector<std::string> lines;
    for (int y = 0; y < renderScreen.dimy(); ++y) {
        std::string line;
        for (int x = 0; x < renderScreen.dimx(); ++x) {
            line += renderScreen.PixelAt(x, y).character;
        }
        lines.push_back(line);
    }
    return lines;
}

std::string TestScreen::captureText() {
    auto lines = captureScreen();
    std::string result;
    for (const auto& line : lines) {
        std::string trimmed = line;
        auto last = trimmed.find_last_not_of(' ');
        if (last != std::string::npos) {
            trimmed.erase(last + 1);
        }
        if (!trimmed.empty()) {
            if (!result.empty()) result += "\n";
            result += trimmed;
        }
    }
    return result;
}

bool TestScreen::waitFor(std::function<bool(const std::string&)> predicate,
                          int timeoutMs) {
    auto start = std::chrono::steady_clock::now();
    while (true) {
        auto text = captureText();
        if (predicate(text)) return true;

        auto elapsed = std::chrono::steady_clock::now() - start;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() >= timeoutMs) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

} // namespace a0::tui::test
