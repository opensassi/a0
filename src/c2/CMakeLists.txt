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
    pthread
    z
)

add_executable(c2 c2_main.cpp)
target_link_libraries(c2 PRIVATE c2_lib)
install(TARGETS c2 RUNTIME DESTINATION bin)
