add_library(core_lib STATIC
    driven_core.cpp
    app_core_thread.cpp
)
target_include_directories(core_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(core_lib PUBLIC
    llm_lib
    executor_lib
    skills_lib
    persistence_lib
    docker_lib
    bootstrap_lib
    ipc_lib
    tui_lib
)
