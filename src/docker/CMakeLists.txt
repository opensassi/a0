add_library(docker_lib STATIC
    docker_cli_wrapper.cpp
    container_manager.cpp
    dependency_installer.cpp
    compose_manager.cpp
    docker_tool_runner.cpp
)

target_include_directories(docker_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(docker_lib PUBLIC shared_lib executor_lib ${CMAKE_DL_LIBS})
