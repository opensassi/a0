add_library(shared_lib INTERFACE)
target_include_directories(shared_lib INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(shared_lib INTERFACE nlohmann_json::nlohmann_json)
