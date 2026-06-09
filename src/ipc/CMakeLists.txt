add_library(ipc_lib STATIC
    unix_socket.cpp
    ipc_protocol.cpp
)
target_include_directories(ipc_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ipc_lib PUBLIC
    shared_lib
    nlohmann_json::nlohmann_json
)

# Cap'n Proto integration (optional)
if(CapnProto_FOUND)
    # Generate C++ from .capnp schema
    set(capnp_schema ${CMAKE_CURRENT_SOURCE_DIR}/app_core_event.capnp)
    set(CAPNPC_IMPORT_DIRS ${CAPNP_INCLUDE_DIRECTORY})
    capnp_generate_cpp(CAPNP_SRCS CAPNP_HDRS ${capnp_schema})

    # Add generated sources to the library
    target_sources(ipc_lib PRIVATE ${CAPNP_SRCS})
    target_include_directories(ipc_lib PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
    target_link_libraries(ipc_lib PUBLIC CapnProto::capnp CapnProto::capnp-rpc)

    # Compile definitions for conditional code
    target_compile_definitions(ipc_lib PRIVATE HAS_CAPNP=1)

    message(STATUS "Cap'n Proto found: ${CapnProto_VERSION} — IPC schema will be compiled")
else()
    message(STATUS "Cap'n Proto NOT found — IPC will use JSON-line framing only")
endif()
