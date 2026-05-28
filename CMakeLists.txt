cmake_minimum_required(VERSION 3.15)
project(a0 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)
find_package(nlohmann_json 3.11.0 REQUIRED)
find_package(GTest REQUIRED)

set(LIB_SOURCES
    src/agent_core.cpp
    src/component_registry.cpp
    src/tool_runner.cpp
    src/skill_runner.cpp
    src/deepseek_provider.cpp
    src/context_manager.cpp
    src/invocation_logger.cpp
    src/schema_inference_engine.cpp
    src/dependency_resolver.cpp
)

add_library(a0_lib STATIC ${LIB_SOURCES})
target_include_directories(a0_lib PUBLIC src)
target_link_libraries(a0_lib PUBLIC CURL::libcurl nlohmann_json::nlohmann_json)

add_executable(a0 src/main.cpp)
target_link_libraries(a0 PRIVATE a0_lib)

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
add_agent_test(test_component_registry test/unit/test_component_registry.cpp)
add_agent_test(test_dependency_resolver test/unit/test_dependency_resolver.cpp)
add_agent_test(test_invocation_logger test/unit/test_invocation_logger.cpp)
add_agent_test(test_schema_inference test/unit/test_schema_inference_engine.cpp)
add_agent_test(test_context_manager test/unit/test_context_manager.cpp)
add_agent_test(test_deepseek_provider test/unit/test_deepseek_provider.cpp)
add_agent_test(test_agent_core test/unit/test_agent_core.cpp)
add_agent_test(test_interfaces test/unit/test_interfaces.cpp)
