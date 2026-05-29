cmake_minimum_required(VERSION 3.15)
project(a0 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)
find_package(nlohmann_json 3.11.0 REQUIRED)
find_package(GTest REQUIRED)
find_package(SQLite3 REQUIRED)

set(LIB_SOURCES
    src/a0_dir.cpp
    src/agent_core.cpp
    src/base_prompt.cpp
    src/skill_registry.cpp
    src/system_tools.cpp
    src/tool_runner.cpp
    src/skill_runner.cpp
    src/deepseek_provider.cpp
    src/context_manager.cpp
    src/invocation_logger.cpp
    src/schema_inference_engine.cpp
    src/dependency_resolver.cpp
)

# CommandRunner — isolated utility with no agent dependencies
add_library(cmd_runner_lib STATIC src/command_runner.cpp)
target_include_directories(cmd_runner_lib PUBLIC src)

# IPC library — Unix sockets + JSON-line protocol
add_library(ipc_lib STATIC
    src/unix_socket.cpp
    src/ipc_protocol.cpp
)
target_include_directories(ipc_lib PUBLIC src)
target_link_libraries(ipc_lib PUBLIC nlohmann_json::nlohmann_json)

add_library(a0_lib STATIC ${LIB_SOURCES})
target_include_directories(a0_lib PUBLIC src)

add_subdirectory(src/docker)
add_subdirectory(src/skills)
add_subdirectory(src/persistence)
add_subdirectory(src/b1)
add_subdirectory(src/c2)

target_link_libraries(a0_lib PUBLIC CURL::libcurl nlohmann_json::nlohmann_json docker_lib skills_lib persistence_lib cmd_runner_lib ipc_lib)

add_executable(a0 src/main.cpp)
target_link_libraries(a0 PRIVATE a0_lib persistence_lib cmd_runner_lib)

option(ENABLE_TRACE "Enable trace logging" OFF)
if(ENABLE_TRACE)
    target_compile_definitions(a0_lib PRIVATE TRACE)
endif()

option(ENABLE_COVERAGE "Enable coverage instrumentation" OFF)
if(ENABLE_COVERAGE)
    target_compile_options(a0_lib PUBLIC --coverage)
    target_link_options(a0_lib PUBLIC --coverage)
endif()

enable_testing()

function(add_agent_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE
        a0_lib
        GTest::gtest
        GTest::gtest_main
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

add_agent_test(test_tool_runner test/unit/test_tool_runner.cpp)
add_agent_test(test_skill_runner test/unit/test_skill_runner.cpp)
add_agent_test(test_skill_registry test/unit/test_skill_registry.cpp)
add_agent_test(test_dependency_resolver test/unit/test_dependency_resolver.cpp)
add_agent_test(test_invocation_logger test/unit/test_invocation_logger.cpp)
add_agent_test(test_schema_inference test/unit/test_schema_inference_engine.cpp)
add_agent_test(test_context_manager test/unit/test_context_manager.cpp)
add_agent_test(test_deepseek_provider test/unit/test_deepseek_provider.cpp)
add_agent_test(test_agent_core test/unit/test_agent_core.cpp)
add_agent_test(test_interfaces test/unit/test_interfaces.cpp)

add_agent_test(test_docker_interfaces test/unit/test_docker_interfaces.cpp)

add_agent_test(test_container_manager test/unit/test_container_manager.cpp)
target_compile_definitions(test_container_manager PRIVATE
    TEST_UNIT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/unit")

add_agent_test(test_dependency_installer test/unit/test_dependency_installer.cpp)
target_compile_definitions(test_dependency_installer PRIVATE
    TEST_UNIT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/unit")

add_agent_test(test_compose_manager test/unit/test_compose_manager.cpp)
target_compile_definitions(test_compose_manager PRIVATE
    TEST_UNIT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/unit")

add_agent_test(test_docker_tool_runner test/unit/test_docker_tool_runner.cpp)
target_compile_definitions(test_docker_tool_runner PRIVATE
    TEST_UNIT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/unit")

# --- IPC tests (standalone — no a0_lib dependency) ---
function(add_ipc_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE
        ipc_lib
        GTest::gtest
        GTest::gtest_main
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

add_ipc_test(test_unix_socket test/unit/test_unix_socket.cpp)
add_ipc_test(test_ipc_protocol test/unit/test_ipc_protocol.cpp)

# --- b1 tests ---
function(add_b1_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE
        b1_lib
        GTest::gtest
        GTest::gtest_main
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

add_b1_test(test_supervisor test/unit/test_supervisor.cpp)
add_b1_test(test_a0_launcher test/unit/test_a0_launcher.cpp)

# --- c2 tests ---
function(add_c2_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE
        c2_lib
        GTest::gtest
        GTest::gtest_main
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

add_c2_test(test_b1_registry test/unit/test_b1_registry.cpp)
add_c2_test(test_c2_listener test/unit/test_c2_listener.cpp)
add_c2_test(test_dashboard_server test/unit/test_dashboard_server.cpp)
