#include "dialog_manager.h"
#include "styles.h"
#include "ftxui/component/component.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/dom/elements.hpp"

namespace a0::tui {

struct DialogEntry {
    ftxui::Component component;
    std::string title;
    std::function<void()> onDismiss;
};

class DialogManager::Impl {
public:
    std::vector<DialogEntry> stack;
    ftxui::Component mainChild;
    ftxui::Component dialogRenderer;
    ftxui::Component mainContainer;
    bool active = false;

    void refresh() {
        active = !stack.empty();
    }
};

DialogManager::DialogManager()
    : m_impl(std::make_unique<Impl>()) {
    m_impl->dialogRenderer = ftxui::Renderer([this] {
        if (m_impl->stack.empty()) {
            return ftxui::emptyElement();
        }
        auto& top = m_impl->stack.back();
        if (top.component) {
            return top.component->Render();
        }
        return ftxui::emptyElement();
    });
    m_impl->mainContainer = ftxui::Container::Vertical({});
}

DialogManager::~DialogManager() = default;

void DialogManager::setMainComponent(ftxui::Component main) {
    m_impl->mainChild = main;
    m_impl->mainContainer = main | ftxui::Modal(m_impl->dialogRenderer, &m_impl->active);
}

ftxui::Component DialogManager::component() const {
    return m_impl->mainContainer;
}

int DialogManager::show(ftxui::Component dialog, const std::string& title,
                         std::function<void()> onDismiss) {
    DialogEntry entry;
    entry.component = dialog;
    entry.title = title;
    entry.onDismiss = std::move(onDismiss);
    m_impl->stack.push_back(std::move(entry));
    m_impl->refresh();
    return 0;
}

void DialogManager::dismiss() {
    if (m_impl->stack.empty()) return;
    auto& top = m_impl->stack.back();
    if (top.onDismiss) top.onDismiss();
    m_impl->stack.pop_back();
    m_impl->refresh();
}

void DialogManager::dismissAll() {
    while (!m_impl->stack.empty()) {
        dismiss();
    }
}

bool DialogManager::isActive() const {
    return m_impl->active;
}

int DialogManager::showHelp() {
    ftxui::Elements contentElems;
    contentElems.push_back(ftxui::text("Keybindings") | ftxui::bold | ftxui::center);
    contentElems.push_back(ftxui::separator());
    contentElems.push_back(ftxui::text("Enter        - Submit input"));
    contentElems.push_back(ftxui::text("Ctrl+C       - Interrupt streaming"));
    contentElems.push_back(ftxui::text("Ctrl+Q       - Quit TUI"));
    contentElems.push_back(ftxui::text("Up/Down      - Input history"));
    contentElems.push_back(ftxui::text("Escape       - Close dialog"));
    contentElems.push_back(ftxui::separator());
    contentElems.push_back(ftxui::text("Commands:") | ftxui::bold);
    contentElems.push_back(ftxui::text("/sessions    - List sessions"));
    contentElems.push_back(ftxui::text("/help        - This help"));
    contentElems.push_back(ftxui::text("/clear       - Clear messages"));
    contentElems.push_back(ftxui::text("/quit        - Exit TUI"));
    contentElems.push_back(ftxui::text("/export      - Export session"));
    contentElems.push_back(ftxui::separator());
    contentElems.push_back(ftxui::text("Press Escape to close") | ftxui::dim);

    auto helpContent = ftxui::vbox(std::move(contentElems));
    auto dialog = ftxui::Renderer([helpContent] { return helpContent; });

    return show(dialog, "Help", nullptr);
}

int DialogManager::showConfirm(const std::string& title,
                                const std::string& message,
                                std::function<void(bool)> onConfirm) {
    ftxui::Elements contentElems;
    contentElems.push_back(ftxui::text(title) | ftxui::bold | ftxui::center);
    contentElems.push_back(ftxui::separator());
    contentElems.push_back(ftxui::text(message));
    contentElems.push_back(ftxui::separator());
    auto content = ftxui::vbox(std::move(contentElems));

    auto dialog = ftxui::Renderer([content] { return content; });
    return show(dialog, title, [onConfirm] {});
}

int DialogManager::showList(const std::string& title,
                             const std::vector<std::pair<std::string, std::string>>& items,
                             std::function<void(const std::string&)> onSelect) {
    ftxui::Elements contentElems;
    contentElems.push_back(ftxui::text(title) | ftxui::bold | ftxui::center);
    contentElems.push_back(ftxui::separator());
    for (const auto& item : items) {
        contentElems.push_back(ftxui::text("  " + item.first));
    }
    contentElems.push_back(ftxui::separator());
    auto content = ftxui::vbox(std::move(contentElems));

    auto dialog = ftxui::Renderer([content] { return content; });
    return show(dialog, title, [onSelect] {
        if (onSelect) onSelect("");
    });
}

} // namespace a0::tui
