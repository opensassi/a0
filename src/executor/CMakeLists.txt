add_library(executor_lib STATIC
    command_runner.cpp
    tool_runner.cpp
    tool_state.cpp
    dependency_graph.cpp
    system_handlers.cpp
    docker_security_filter.cpp
    stream_registry.cpp
)
target_include_directories(executor_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(executor_lib PUBLIC shared_lib skills_lib)
