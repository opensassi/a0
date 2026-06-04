#pragma once

#include <string>
#include <memory>
#include <functional>
#include <cstdint>
#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "styles.h"

namespace a0::tui {

class StatusBar {
public:
    StatusBar();
    virtual ~StatusBar();

    ftxui::Component component() const;

    void setSessionId(const std::string& uuid);
    void setAgentState(AgentState state);
    void setB1Connected(bool connected);
    void setMessageCount(size_t count);
    void showStatus(const std::string& msg, int timeoutSecs = 3);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace a0::tui
