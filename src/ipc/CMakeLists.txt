add_library(ipc_lib STATIC
    unix_socket.cpp
    ipc_protocol.cpp
)
target_include_directories(ipc_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ipc_lib PUBLIC
    shared_lib
    nlohmann_json::nlohmann_json
)
