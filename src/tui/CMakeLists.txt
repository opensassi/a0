add_library(tui_lib STATIC
    styles.cpp
    agent_tui.cpp
    message_panel.cpp
    input_panel.cpp
    status_bar.cpp
    dialog_manager.cpp
    session_manager.cpp
    markdown_renderer.cpp
    clipboard.cpp
)
target_include_directories(tui_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(tui_lib PRIVATE ${md4c_SOURCE_DIR}/src)
target_link_libraries(tui_lib PUBLIC
    ftxui::component
    md4c
    a0_lib
    persistence_lib
    nlohmann_json::nlohmann_json
)
