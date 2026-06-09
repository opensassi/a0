add_library(skills_lib STATIC
    skill_manager.cpp
    skill_loader.cpp
    version_manager.cpp
    validation_engine.cpp
)

target_include_directories(skills_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/thirdparty
)
target_link_libraries(skills_lib PUBLIC
    shared_lib
    executor_lib
    nlohmann_json::nlohmann_json
)
