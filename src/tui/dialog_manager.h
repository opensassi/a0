#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <utility>
#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"

namespace a0::tui {

class DialogManager {
public:
    DialogManager();
    virtual ~DialogManager();

    ftxui::Component component() const;

    void setMainComponent(ftxui::Component main);

    int show(ftxui::Component dialog, const std::string& title,
             std::function<void()> onDismiss = nullptr);

    void dismiss();
    void dismissAll();

    bool isActive() const;

    int showHelp();
    int showConfirm(const std::string& title,
                    const std::string& message,
                    std::function<void(bool)> onConfirm);

    int showList(const std::string& title,
                 const std::vector<std::pair<std::string, std::string>>& items,
                 std::function<void(const std::string&)> onSelect);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace a0::tui
