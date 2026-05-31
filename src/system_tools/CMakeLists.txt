add_library(system_tools_lib STATIC
    registry.cpp
    core_handlers.cpp
    git_handlers.cpp
    discovery_handlers.cpp
    ../docker_security_filter.cpp
)
target_include_directories(system_tools_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(system_tools_lib PUBLIC nlohmann_json::nlohmann_json cmd_runner_lib skills_lib)
