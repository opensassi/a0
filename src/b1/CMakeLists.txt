add_library(b1_lib STATIC
    supervisor.cpp
    a0_launcher.cpp
)
target_include_directories(b1_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(b1_lib PUBLIC ipc_lib cmd_runner_lib nlohmann_json::nlohmann_json)

add_executable(b1 b1_main.cpp)
target_link_libraries(b1 PRIVATE b1_lib)
set_target_properties(b1 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
install(TARGETS b1 RUNTIME DESTINATION bin)
