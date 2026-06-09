include(FetchContent)
FetchContent_Declare(
    uWebSockets
    GIT_REPOSITORY https://github.com/uNetworking/uWebSockets.git
    GIT_TAG v20.77.0
    GIT_SHALLOW TRUE
    GIT_SUBMODULES uSockets
)
FetchContent_MakeAvailable(uWebSockets)

find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED)

set(USOCKETS_SRC_DIR "${uwebsockets_SOURCE_DIR}/uSockets/src")
set(USOCKETS_SOURCES
    "${USOCKETS_SRC_DIR}/bsd.c"
    "${USOCKETS_SRC_DIR}/context.c"
    "${USOCKETS_SRC_DIR}/loop.c"
    "${USOCKETS_SRC_DIR}/quic.c"
    "${USOCKETS_SRC_DIR}/socket.c"
    "${USOCKETS_SRC_DIR}/udp.c"
    "${USOCKETS_SRC_DIR}/eventing/epoll_kqueue.c"
    "${USOCKETS_SRC_DIR}/crypto/openssl.c"
    "${USOCKETS_SRC_DIR}/crypto/sni_tree.cpp"
)

add_library(uSockets STATIC ${USOCKETS_SOURCES})
target_include_directories(uSockets PUBLIC
    ${USOCKETS_SRC_DIR}
    ${USOCKETS_SRC_DIR}/crypto
    ${USOCKETS_SRC_DIR}/eventing
    ${USOCKETS_SRC_DIR}/internal
)
target_compile_definitions(uSockets PRIVATE
    LIBUS_USE_EPOLL
    LIBUS_USE_OPENSSL
)
target_link_libraries(uSockets PUBLIC
    OpenSSL::SSL
    OpenSSL::Crypto
)

add_library(c2_lib STATIC
    b1_registry.cpp
    c2_listener.cpp
    dashboard_server.cpp
    sse_manager.cpp
    c2_event_store.cpp
)
target_include_directories(c2_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${uwebsockets_SOURCE_DIR}/src
)
target_link_libraries(c2_lib PUBLIC
    ipc_lib
    nlohmann_json::nlohmann_json
    uSockets
    SQLite::SQLite3
    pthread
    z
)

# Copy web UI files to share directory for default web-root resolution
file(COPY ${CMAKE_SOURCE_DIR}/c2/web DESTINATION ${CMAKE_BINARY_DIR}/share/c2)

add_executable(c2 c2_main.cpp)
target_link_libraries(c2 PRIVATE c2_lib)
target_compile_definitions(c2 PRIVATE
    C2_DEFAULT_WEB_ROOT="${CMAKE_BINARY_DIR}/share/c2/web"
)
set_target_properties(c2 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
install(TARGETS c2 RUNTIME DESTINATION bin)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/c2/web DESTINATION ${CMAKE_INSTALL_PREFIX}/share/c2)
