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
target_link_libraries(skills_lib PUBLIC nlohmann_json::nlohmann_json cmd_runner_lib)
