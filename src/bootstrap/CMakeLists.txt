add_library(bootstrap_lib STATIC
    a0_dir.cpp
    base_prompt.cpp
    personas.cpp
    session_context.cpp
)
target_include_directories(bootstrap_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bootstrap_lib PUBLIC
    shared_lib
    persistence_lib
    skills_lib
)
