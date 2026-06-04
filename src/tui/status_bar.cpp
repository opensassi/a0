#include "status_bar.h"
#include "ftxui/component/component.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/dom/elements.hpp"
#include <sstream>
#include <chrono>

namespace a0::tui {

class StatusBar::Impl {
public:
    std::string sessionId;
    AgentState agentState = AgentState::Idle;
    bool b1Connected = false;
    size_t messageCount = 0;
    std::string flashMessage;
    int flashTimeoutSecs = 3;
    bool showingFlash = false;
    std::chrono::steady_clock::time_point flashStart;

    bool isFlashExpired() const {
        if (!showingFlash) return true;
        return std::chrono::steady_clock::now() - flashStart >
               std::chrono::seconds(flashTimeoutSecs);
    }

    ftxui::Component renderer;

    std::string xAgentLabel(AgentState s) const {
        switch (s) {
            case AgentState::Idle:       return "Idle";
            case AgentState::Thinking:   return "Thinking";
            case AgentState::Executing:  return "Executing";
            case AgentState::Error:      return "Error";
        }
        return "Unknown";
    }

    ftxui::Color xAgentColor(AgentState s) const {
        switch (s) {
            case AgentState::Idle:       return ftxui::Color::Default;
            case AgentState::Thinking:   return ftxui::Color::Yellow;
            case AgentState::Executing:  return ftxui::Color::Blue;
            case AgentState::Error:      return ftxui::Color::Red;
        }
        return ftxui::Color::Default;
    }
};

StatusBar::StatusBar()
    : m_impl(std::make_unique<Impl>()) {
    m_impl->renderer = ftxui::Renderer([this] {
        auto& d = *m_impl;

        if (d.isFlashExpired()) {
            d.showingFlash = false;
        }

        if (d.showingFlash && !d.flashMessage.empty()) {
            ftxui::Elements flashElems;
            flashElems.push_back(ftxui::text(d.flashMessage)
                | ftxui::color(ftxui::Color::Green)
                | ftxui::bold);
            return ftxui::hbox(std::move(flashElems));
        }

        ftxui::Elements elems;

        if (!d.sessionId.empty()) {
            auto id = d.sessionId;
            if (id.length() > 8) id = id.substr(0, 8);
            elems.push_back(ftxui::text(id) | ftxui::color(ftxui::Color::GrayDark));
        }

        elems.push_back(ftxui::separator());

        auto stateColor = d.xAgentColor(d.agentState);
        elems.push_back(ftxui::text(d.xAgentLabel(d.agentState))
            | ftxui::bold
            | ftxui::color(stateColor));

        elems.push_back(ftxui::separator());

        if (d.b1Connected) {
            elems.push_back(ftxui::text("b1: OK") | ftxui::color(ftxui::Color::Green));
        } else {
            elems.push_back(ftxui::text("b1: --") | ftxui::color(ftxui::Color::Red));
        }

        elems.push_back(ftxui::separator());

        std::ostringstream oss;
        oss << d.messageCount << " msgs";
        elems.push_back(ftxui::text(oss.str()) | ftxui::color(ftxui::Color::GrayDark));

        return ftxui::hbox(std::move(elems));
    });
}

StatusBar::~StatusBar() = default;

ftxui::Component StatusBar::component() const {
    return m_impl->renderer;
}

void StatusBar::setSessionId(const std::string& uuid) {
    m_impl->sessionId = uuid;
}

void StatusBar::setAgentState(AgentState state) {
    m_impl->agentState = state;
}

void StatusBar::setB1Connected(bool connected) {
    m_impl->b1Connected = connected;
}

void StatusBar::setMessageCount(size_t count) {
    m_impl->messageCount = count;
}

void StatusBar::showStatus(const std::string& msg, int timeoutSecs) {
    m_impl->flashMessage = msg;
    m_impl->flashTimeoutSecs = timeoutSecs;
    m_impl->flashStart = std::chrono::steady_clock::now();
    m_impl->showingFlash = true;
}

} // namespace a0::tui
